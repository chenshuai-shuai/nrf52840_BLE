#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "error.h"
#include "hal_gps.h"

LOG_MODULE_REGISTER(gps_nrf, LOG_LEVEL_INF);

#define GPS_UART_NODE DT_NODELABEL(uart1)
#if !DT_NODE_HAS_STATUS(GPS_UART_NODE, okay)
#error "uart1 is not enabled in devicetree for GPS"
#endif

#define GPS_LINE_QUEUE_LEN 32

typedef struct {
    hal_gps_packet_t pkt;
} gps_line_item_t;

K_MSGQ_DEFINE(g_gps_line_q, sizeof(gps_line_item_t), GPS_LINE_QUEUE_LEN, 4);

static struct {
    const struct device *uart;
    struct uart_config cfg;
    struct k_mutex lock;
    char line_buf[HAL_GPS_PACKET_MAX_LEN];
    uint16_t line_len;
    bool inited;
    bool started;
} g_gps;

static void gps_uart_isr(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    if (!uart_irq_update(dev)) {
        return;
    }

    while (uart_irq_rx_ready(dev)) {
        uint8_t c = 0;
        int rd = uart_fifo_read(dev, &c, 1);
        if (rd <= 0) {
            break;
        }

        if (g_gps.line_len < (HAL_GPS_PACKET_MAX_LEN - 1U)) {
            g_gps.line_buf[g_gps.line_len++] = (char)c;
        } else {
            g_gps.line_len = 0;
        }

        if (c == '\n') {
            gps_line_item_t item;
            memset(&item, 0, sizeof(item));
            item.pkt.len = g_gps.line_len;
            if (item.pkt.len >= HAL_GPS_PACKET_MAX_LEN) {
                item.pkt.len = HAL_GPS_PACKET_MAX_LEN - 1U;
            }
            memcpy(item.pkt.sentence, g_gps.line_buf, item.pkt.len);
            item.pkt.sentence[item.pkt.len] = '\0';
            item.pkt.timestamp_ms = (uint32_t)k_uptime_get();
            g_gps.line_len = 0;

            if (k_msgq_put(&g_gps_line_q, &item, K_NO_WAIT) != 0) {
                gps_line_item_t drop;
                if (k_msgq_get(&g_gps_line_q, &drop, K_NO_WAIT) == 0) {
                    (void)k_msgq_put(&g_gps_line_q, &item, K_NO_WAIT);
                }
            }
        }
    }
}

static int gps_nrf_init(void)
{
    if (g_gps.inited) {
        return HAL_OK;
    }

    g_gps.uart = DEVICE_DT_GET(GPS_UART_NODE);
    if (!device_is_ready(g_gps.uart)) {
        LOG_ERR("gps uart not ready");
        return HAL_ENODEV;
    }

    g_gps.cfg = (struct uart_config) {
        .baudrate = 115200U,
        .parity = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
    };

    int ret = uart_configure(g_gps.uart, &g_gps.cfg);
    if (ret != 0) {
        LOG_ERR("gps uart configure failed: %d", ret);
        return HAL_EIO;
    }

    k_mutex_init(&g_gps.lock);
    g_gps.line_len = 0U;
    g_gps.inited = true;
    LOG_INF("gps init: uart=%s baud=%u", g_gps.uart->name, g_gps.cfg.baudrate);
    return HAL_OK;
}

static int gps_nrf_start(void)
{
    if (!g_gps.inited) {
        int ret = gps_nrf_init();
        if (ret != HAL_OK) {
            return ret;
        }
    }

    k_mutex_lock(&g_gps.lock, K_FOREVER);
    if (g_gps.started) {
        k_mutex_unlock(&g_gps.lock);
        return HAL_OK;
    }

    uart_irq_rx_disable(g_gps.uart);
    uart_irq_callback_user_data_set(g_gps.uart, gps_uart_isr, NULL);
    uart_irq_rx_enable(g_gps.uart);
    g_gps.started = true;
    k_mutex_unlock(&g_gps.lock);

    LOG_INF("gps start");
    return HAL_OK;
}

static int gps_nrf_stop(void)
{
    if (!g_gps.inited) {
        return HAL_ENODEV;
    }

    k_mutex_lock(&g_gps.lock, K_FOREVER);
    if (g_gps.started) {
        uart_irq_rx_disable(g_gps.uart);
        g_gps.started = false;
    }
    k_mutex_unlock(&g_gps.lock);

    return HAL_OK;
}

static int gps_nrf_read(void *buf, size_t len, int timeout_ms)
{
    if (!g_gps.inited || !g_gps.started) {
        return HAL_ENODEV;
    }
    if (buf == NULL || len < sizeof(hal_gps_packet_t)) {
        return HAL_EINVAL;
    }

    gps_line_item_t item;
    k_timeout_t to = (timeout_ms < 0) ? K_FOREVER : K_MSEC(timeout_ms);
    int ret = k_msgq_get(&g_gps_line_q, &item, to);
    if (ret != 0) {
        return (ret == -EAGAIN) ? HAL_EBUSY : HAL_EIO;
    }

    memcpy(buf, &item.pkt, sizeof(item.pkt));
    return HAL_OK;
}

static const hal_gps_ops_t g_gps_ops = {
    .init = gps_nrf_init,
    .start = gps_nrf_start,
    .stop = gps_nrf_stop,
    .read = gps_nrf_read,
};

int gps_nrf_register(void)
{
    return hal_gps_register(&g_gps_ops);
}


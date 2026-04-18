#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "app_mode_manager.h"
#include "app_wifi_boot_ctrl.h"
#include "error.h"

LOG_MODULE_REGISTER(app_wifi_boot_ctrl, LOG_LEVEL_INF);

#define WIFI_BOOT_CTRL_NODE DT_ALIAS(wifi_boot_ctrl)
#define WIFI_EN_CTRL_NODE   DT_ALIAS(wifi_en_ctrl)
#define WIFI_CMD_UART_NODE  DT_NODELABEL(uart1)

#if !DT_NODE_HAS_STATUS(WIFI_BOOT_CTRL_NODE, okay)
#error "wifi-boot-ctrl alias is missing"
#endif

#if !DT_NODE_HAS_STATUS(WIFI_EN_CTRL_NODE, okay)
#error "wifi-en-ctrl alias is missing"
#endif

#if !DT_NODE_HAS_STATUS(WIFI_CMD_UART_NODE, okay)
#error "uart1 must be enabled for ESP boot control commands"
#endif

#define WIFI_CMD_STACK_SIZE 1536
#define WIFI_CMD_PRIO       4

#define WIFI_BOOT_ASSERT_MS     10
#define WIFI_RESET_PULSE_MS     80
#define WIFI_BOOT_RELEASE_MS    120

#define WIFI_CMD_ENTER_DL       "NRF:ESP_DL"
#define WIFI_CMD_BOOT_RUN       "NRF:ESP_BOOT"
#define WIFI_CMD_RESET          "NRF:ESP_RST"
#define WIFI_CMD_FW_BEGIN       "NRF:ESP_FW_BEGIN"
#define WIFI_CMD_FW_END         "NRF:ESP_FW_END"

#define WIFI_CMD_IDLE_FLUSH_MS  30
#define WIFI_CMD_PREFIX         "NRF:"
#define WIFI_CMD_RX_Q_LEN       128

static const struct gpio_dt_spec g_boot_ctrl = GPIO_DT_SPEC_GET(WIFI_BOOT_CTRL_NODE, gpios);
static const struct gpio_dt_spec g_wifi_en = GPIO_DT_SPEC_GET(WIFI_EN_CTRL_NODE, gpios);
static const struct device *const g_uart = DEVICE_DT_GET(WIFI_CMD_UART_NODE);

static struct k_thread g_wifi_cmd_thread;
K_THREAD_STACK_DEFINE(g_wifi_cmd_stack, WIFI_CMD_STACK_SIZE);
K_MSGQ_DEFINE(g_wifi_cmd_rx_q, sizeof(uint8_t), WIFI_CMD_RX_Q_LEN, 1);

static bool g_wifi_boot_ctrl_prepared;
static bool g_wifi_boot_ctrl_started;

static int wifi_boot_gpio_init(void)
{
    if (!gpio_is_ready_dt(&g_boot_ctrl) || !gpio_is_ready_dt(&g_wifi_en)) {
        return HAL_ENODEV;
    }

    int ret = gpio_pin_configure_dt(&g_boot_ctrl, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
        return ret;
    }

    ret = gpio_pin_configure_dt(&g_wifi_en, GPIO_OUTPUT_ACTIVE);
    if (ret != 0) {
        return ret;
    }

    return HAL_OK;
}

static void wifi_boot_ctrl_set(bool asserted)
{
    (void)gpio_pin_set_dt(&g_boot_ctrl, asserted ? 1 : 0);
}

static void wifi_en_reset_pulse(void)
{
    (void)gpio_pin_set_dt(&g_wifi_en, 1);
    k_msleep(WIFI_RESET_PULSE_MS);
    (void)gpio_pin_set_dt(&g_wifi_en, 0);
}

static int wifi_boot_enter_update_mode(void)
{
    int ret = app_mode_enter_esp_fw_update();
    if (ret != HAL_OK) {
        LOG_ERR("wifi boot: enter update mode failed: %d", ret);
        return ret;
    }

    ret = app_wifi_boot_ctrl_enter_download();
    if (ret != HAL_OK) {
        LOG_ERR("wifi boot: download mode failed: %d", ret);
        (void)app_mode_exit_esp_fw_update();
        return ret;
    }

    return HAL_OK;
}

static int wifi_boot_exit_update_mode(void)
{
    int ret = app_wifi_boot_ctrl_boot_normal();
    if (ret != HAL_OK) {
        LOG_ERR("wifi boot: boot normal failed: %d", ret);
        return ret;
    }

    ret = app_mode_exit_esp_fw_update();
    if (ret != HAL_OK) {
        LOG_ERR("wifi boot: exit update mode failed: %d", ret);
        return ret;
    }

    return HAL_OK;
}

int app_wifi_boot_ctrl_prepare(void)
{
    int ret;

    if (g_wifi_boot_ctrl_prepared) {
        return HAL_OK;
    }

    ret = wifi_boot_gpio_init();
    if (ret != HAL_OK) {
        LOG_ERR("wifi boot: gpio init failed: %d", ret);
        return ret;
    }

    g_wifi_boot_ctrl_prepared = true;
    LOG_INF("wifi boot: esp held in reset awaiting command");
    return HAL_OK;
}

int app_wifi_boot_ctrl_enter_download(void)
{
    wifi_boot_ctrl_set(true);
    k_msleep(WIFI_BOOT_ASSERT_MS);
    wifi_en_reset_pulse();
    k_msleep(WIFI_BOOT_RELEASE_MS);
    LOG_INF("wifi boot: download mode asserted");
    return HAL_OK;
}

int app_wifi_boot_ctrl_boot_normal(void)
{
    wifi_boot_ctrl_set(false);
    k_msleep(WIFI_BOOT_ASSERT_MS);
    wifi_en_reset_pulse();
    k_msleep(WIFI_BOOT_RELEASE_MS);
    LOG_INF("wifi boot: normal boot asserted");
    return HAL_OK;
}

int app_wifi_boot_ctrl_reset_only(void)
{
    wifi_en_reset_pulse();
    k_msleep(WIFI_BOOT_RELEASE_MS);
    LOG_INF("wifi boot: reset pulse done");
    return HAL_OK;
}

static void wifi_boot_handle_command(const char *cmd)
{
    if (strcmp(cmd, WIFI_CMD_ENTER_DL) == 0) {
        LOG_INF("cmd match: ESP_DL");
        (void)wifi_boot_enter_update_mode();
        return;
    }

    if (strcmp(cmd, WIFI_CMD_BOOT_RUN) == 0) {
        LOG_INF("cmd match: ESP_BOOT");
        (void)wifi_boot_exit_update_mode();
        return;
    }

    if (strcmp(cmd, WIFI_CMD_RESET) == 0) {
        LOG_INF("cmd match: ESP_RST");
        (void)app_wifi_boot_ctrl_reset_only();
        return;
    }

    if (strcmp(cmd, WIFI_CMD_FW_BEGIN) == 0) {
        LOG_INF("cmd match: ESP_FW_BEGIN");
        (void)wifi_boot_enter_update_mode();
        return;
    }

    if (strcmp(cmd, WIFI_CMD_FW_END) == 0) {
        LOG_INF("cmd match: ESP_FW_END");
        (void)wifi_boot_exit_update_mode();
        return;
    }

    LOG_INF("cmd unknown: %s", cmd);
}

static void wifi_uart_isr(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    if (!uart_irq_update(dev)) {
        return;
    }

    if (!uart_irq_rx_ready(dev)) {
        return;
    }

    uint8_t buf[16];
    int len = uart_fifo_read(dev, buf, sizeof(buf));
    for (int i = 0; i < len; i++) {
        (void)k_msgq_put(&g_wifi_cmd_rx_q, &buf[i], K_NO_WAIT);
    }
}

static void wifi_cmd_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    char line[32];
    size_t pos = 0;
    int64_t last_rx_ms = 0;
    size_t prefix_match = 0;

    LOG_INF("wifi boot cmd: uart1 listen on P0.28 @ 115200");
    while (1) {
        uint8_t ch = 0U;
        k_timeout_t timeout = (pos > 0U) ? K_MSEC(WIFI_CMD_IDLE_FLUSH_MS) : K_FOREVER;
        int ret = k_msgq_get(&g_wifi_cmd_rx_q, &ch, timeout);
        if (ret != 0) {
            if ((pos > 0U) && (last_rx_ms > 0)) {
                pos = 0U;
                last_rx_ms = 0;
                prefix_match = 0U;
            }
            continue;
        }

        last_rx_ms = k_uptime_get();

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            if (pos > 0U) {
                line[pos] = '\0';
                LOG_INF("rx line: %s", line);
                wifi_boot_handle_command(line);
                pos = 0U;
                last_rx_ms = 0;
            }
            prefix_match = 0U;
            continue;
        }

        if (pos == 0U) {
            if (ch == (unsigned char)WIFI_CMD_PREFIX[prefix_match]) {
                prefix_match++;
                if (prefix_match == sizeof(WIFI_CMD_PREFIX) - 1U) {
                    memcpy(line, WIFI_CMD_PREFIX, sizeof(WIFI_CMD_PREFIX) - 1U);
                    pos = sizeof(WIFI_CMD_PREFIX) - 1U;
                    prefix_match = 0U;
                }
            } else {
                prefix_match = (ch == (unsigned char)WIFI_CMD_PREFIX[0]) ? 1U : 0U;
            }
            continue;
        }

        if ((ch < 0x20U) || (ch > 0x7eU)) {
            pos = 0U;
            last_rx_ms = 0;
            prefix_match = 0U;
            continue;
        }

        if (pos < (sizeof(line) - 1U)) {
            line[pos++] = (char)ch;
        } else {
            pos = 0U;
            last_rx_ms = 0;
            prefix_match = 0U;
        }
    }
}

int app_wifi_boot_ctrl_start(void)
{
    int ret;

    if (g_wifi_boot_ctrl_started) {
        return HAL_OK;
    }

    ret = app_wifi_boot_ctrl_prepare();
    if (ret != HAL_OK) {
        return ret;
    }

    if (!device_is_ready(g_uart)) {
        LOG_ERR("wifi boot: uart1 not ready");
        return HAL_ENODEV;
    }

    uart_irq_callback_user_data_set(g_uart, wifi_uart_isr, NULL);
    uart_irq_rx_enable(g_uart);

    k_thread_create(&g_wifi_cmd_thread,
                    g_wifi_cmd_stack,
                    K_THREAD_STACK_SIZEOF(g_wifi_cmd_stack),
                    wifi_cmd_entry,
                    NULL, NULL, NULL,
                    WIFI_CMD_PRIO,
                    0,
                    K_NO_WAIT);
    k_thread_name_set(&g_wifi_cmd_thread, "wifi_boot");

    g_wifi_boot_ctrl_started = true;
    LOG_INF("wifi boot: control started");
    return HAL_OK;
}

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include "app_mode_manager.h"
#include "app_audio_route.h"
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
#define WIFI_CMD_ESP_PING       "NRF:ESP_PING"
#define WIFI_CMD_CONV_START     "NRF:CONV_START"
#define WIFI_CMD_CONV_STOP      "NRF:CONV_STOP"
#define WIFI_CMD_CONV_END       "NRF:CONV_END"

#define WIFI_CMD_IDLE_FLUSH_MS  30
#define WIFI_CMD_RX_Q_LEN       128
#define WIFI_LINE_MAX_LEN       96

#define WIFI_ESP_RSP_TIMEOUT_MS 1000
#define WIFI_ESP_RETRY_MAX      3
/* Temporarily disable nRF -> ESP UART command control. */
#define WIFI_ESP_UART_CMD_ENABLE 0

enum esp_ctrl_cmd {
    ESP_CTRL_CMD_NONE = 0,
    ESP_CTRL_CMD_PING,
    ESP_CTRL_CMD_CONV_START,
    ESP_CTRL_CMD_CONV_STOP,
};

static const struct gpio_dt_spec g_boot_ctrl = GPIO_DT_SPEC_GET(WIFI_BOOT_CTRL_NODE, gpios);
static const struct gpio_dt_spec g_wifi_en = GPIO_DT_SPEC_GET(WIFI_EN_CTRL_NODE, gpios);
static const struct device *const g_uart = DEVICE_DT_GET(WIFI_CMD_UART_NODE);

static struct k_thread g_wifi_cmd_thread;
K_THREAD_STACK_DEFINE(g_wifi_cmd_stack, WIFI_CMD_STACK_SIZE);
K_MSGQ_DEFINE(g_wifi_cmd_rx_q, sizeof(uint8_t), WIFI_CMD_RX_Q_LEN, 1);
K_MUTEX_DEFINE(g_esp_cmd_api_lock);
K_SEM_DEFINE(g_esp_cmd_rsp_sem, 0, 1);

static bool g_wifi_boot_ctrl_prepared;
static bool g_wifi_boot_ctrl_started;
static volatile bool g_esp_cmd_waiting;
static volatile int g_esp_cmd_result;
static volatile enum esp_ctrl_cmd g_esp_cmd_type = ESP_CTRL_CMD_NONE;
static char g_esp_rsp_line[WIFI_LINE_MAX_LEN];

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

    if (strcmp(cmd, WIFI_CMD_ESP_PING) == 0) {
        int ret = app_wifi_boot_ctrl_ping();
        LOG_INF("cmd NRF:ESP_PING ret=%d", ret);
        return;
    }

    if (strcmp(cmd, WIFI_CMD_CONV_START) == 0) {
        int ret = app_wifi_boot_ctrl_conv_start();
        LOG_INF("cmd NRF:CONV_START ret=%d", ret);
        return;
    }

    if ((strcmp(cmd, WIFI_CMD_CONV_STOP) == 0) ||
        (strcmp(cmd, WIFI_CMD_CONV_END) == 0)) {
        int ret = app_wifi_boot_ctrl_conv_stop();
        LOG_INF("cmd %s ret=%d", cmd, ret);
        return;
    }

    LOG_INF("cmd unknown: %s", cmd);
}

static bool line_has_prefix(const char *line, const char *prefix)
{
    if (line == NULL || prefix == NULL) {
        return false;
    }
    size_t n = strlen(prefix);
    return strncmp(line, prefix, n) == 0;
}

static bool esp_cmd_resp_matches(enum esp_ctrl_cmd cmd, const char *line)
{
    if (line == NULL) {
        return false;
    }

    if (line_has_prefix(line, "ERR:")) {
        return true;
    }

    switch (cmd) {
    case ESP_CTRL_CMD_PING:
        return strcmp(line, "OK:PONG") == 0;
    case ESP_CTRL_CMD_CONV_START:
        return line_has_prefix(line, "OK:CONV_START:");
    case ESP_CTRL_CMD_CONV_STOP:
        return (strcmp(line, "OK:CONV_STOP") == 0) ||
               (strcmp(line, "OK:CONV_STOP:NO_ACTIVE_SESSION") == 0) ||
               (strcmp(line, "OK:CONV_END") == 0) ||
               (strcmp(line, "OK:CONV_END:NO_ACTIVE_SESSION") == 0);
    case ESP_CTRL_CMD_NONE:
    default:
        return false;
    }
}

static void esp_cmd_maybe_complete(const char *line)
{
    if (!g_esp_cmd_waiting || line == NULL) {
        return;
    }

    enum esp_ctrl_cmd cmd = g_esp_cmd_type;
    if (!esp_cmd_resp_matches(cmd, line)) {
        return;
    }

    size_t n = strlen(line);
    if (n >= sizeof(g_esp_rsp_line)) {
        n = sizeof(g_esp_rsp_line) - 1U;
    }
    memcpy(g_esp_rsp_line, line, n);
    g_esp_rsp_line[n] = '\0';

    if (line_has_prefix(line, "ERR:")) {
        g_esp_cmd_result = HAL_EIO;
    } else {
        g_esp_cmd_result = HAL_OK;
    }

    g_esp_cmd_waiting = false;
    k_sem_give(&g_esp_cmd_rsp_sem);
}

static void wifi_uart_handle_line(const char *line)
{
    if (line == NULL || line[0] == '\0') {
        return;
    }

    if (line_has_prefix(line, "NRF:")) {
        LOG_INF("rx host line: %s", line);
        wifi_boot_handle_command(line);
        return;
    }

    LOG_INF("rx esp line: %s", line);
    esp_cmd_maybe_complete(line);
}

static int esp_uart_send_line(const char *line)
{
    if (line == NULL || line[0] == '\0') {
        return HAL_EINVAL;
    }
    if (!g_wifi_boot_ctrl_started || !device_is_ready(g_uart)) {
        return HAL_ENODEV;
    }

    size_t n = strlen(line);
    for (size_t i = 0; i < n; i++) {
        uart_poll_out(g_uart, (unsigned char)line[i]);
    }
    uart_poll_out(g_uart, '\r');
    uart_poll_out(g_uart, '\n');
    return HAL_OK;
}

static void esp_cmd_drain_rsp_sem(void)
{
    while (k_sem_take(&g_esp_cmd_rsp_sem, K_NO_WAIT) == 0) {
    }
}

static int esp_cmd_request_with_retry(enum esp_ctrl_cmd cmd, const char *line)
{
#if !WIFI_ESP_UART_CMD_ENABLE
    ARG_UNUSED(cmd);
    ARG_UNUSED(line);
    return HAL_OK;
#else
    static const uint16_t retry_backoff_ms[WIFI_ESP_RETRY_MAX] = { 200U, 500U, 1000U };

    int lock_ret = k_mutex_lock(&g_esp_cmd_api_lock, K_MSEC(2000));
    if (lock_ret != 0) {
        return HAL_EBUSY;
    }

    int ret = -ETIMEDOUT;
    for (size_t attempt = 0; attempt <= WIFI_ESP_RETRY_MAX; attempt++) {
        esp_cmd_drain_rsp_sem();
        g_esp_cmd_type = cmd;
        g_esp_cmd_result = HAL_EBUSY;
        g_esp_cmd_waiting = true;
        g_esp_rsp_line[0] = '\0';

        int sret = esp_uart_send_line(line);
        if (sret != HAL_OK) {
            g_esp_cmd_waiting = false;
            g_esp_cmd_type = ESP_CTRL_CMD_NONE;
            ret = sret;
            break;
        }

        int wret = k_sem_take(&g_esp_cmd_rsp_sem, K_MSEC(WIFI_ESP_RSP_TIMEOUT_MS));
        if (wret == 0) {
            ret = g_esp_cmd_result;
            g_esp_cmd_type = ESP_CTRL_CMD_NONE;
            if (ret == HAL_OK) {
                LOG_INF("esp cmd ok: %s -> %s", line, g_esp_rsp_line);
            } else {
                LOG_WRN("esp cmd err: %s -> %s", line, g_esp_rsp_line);
            }
            break;
        }

        g_esp_cmd_waiting = false;
        g_esp_cmd_type = ESP_CTRL_CMD_NONE;
        ret = -ETIMEDOUT;
        LOG_WRN("esp cmd timeout: %s (attempt %u/%u)",
                line,
                (unsigned int)(attempt + 1U),
                (unsigned int)(WIFI_ESP_RETRY_MAX + 1U));

        if (attempt < WIFI_ESP_RETRY_MAX) {
            k_msleep(retry_backoff_ms[attempt]);
        }
    }

    k_mutex_unlock(&g_esp_cmd_api_lock);
    return ret;
#endif
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

    char line[WIFI_LINE_MAX_LEN];
    size_t pos = 0U;
    int64_t last_rx_ms = 0;
    bool overflow = false;

    LOG_INF("wifi boot cmd: uart1 listen on P0.28 @ 115200");
    while (1) {
        uint8_t ch = 0U;
        k_timeout_t timeout = (pos > 0U || overflow) ? K_MSEC(WIFI_CMD_IDLE_FLUSH_MS) : K_FOREVER;
        int ret = k_msgq_get(&g_wifi_cmd_rx_q, &ch, timeout);
        if (ret != 0) {
            if ((pos > 0U || overflow) && (last_rx_ms > 0)) {
                pos = 0U;
                overflow = false;
                last_rx_ms = 0;
            }
            continue;
        }

        last_rx_ms = k_uptime_get();

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            if (overflow) {
                LOG_WRN("uart line overflow, drop");
            } else if (pos > 0U && pos < sizeof(line)) {
                line[pos] = '\0';
                wifi_uart_handle_line(line);
            }
            pos = 0U;
            overflow = false;
            last_rx_ms = 0;
            continue;
        }

        if ((ch < 0x20U) || (ch > 0x7eU)) {
            pos = 0U;
            overflow = false;
            last_rx_ms = 0;
            continue;
        }

        if (overflow) {
            continue;
        }

        if (pos < (sizeof(line) - 1U)) {
            line[pos++] = (char)ch;
        } else {
            overflow = true;
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

int app_wifi_boot_ctrl_ping(void)
{
    return esp_cmd_request_with_retry(ESP_CTRL_CMD_PING, "ESP:PING");
}

int app_wifi_boot_ctrl_conv_start(void)
{
    int ret = app_audio_route_request_wifi();
    if (ret != HAL_OK) {
        LOG_ERR("wifi conv start: audio route request failed: %d", ret);
        return ret;
    }

    ret = esp_cmd_request_with_retry(ESP_CTRL_CMD_CONV_START, "ESP:CONV_START");
    if (ret != HAL_OK) {
        LOG_ERR("wifi conv start: esp cmd failed: %d", ret);
        (void)app_audio_route_release_wifi();
        return ret;
    }

    return HAL_OK;
}

int app_wifi_boot_ctrl_conv_stop(void)
{
    int ret = esp_cmd_request_with_retry(ESP_CTRL_CMD_CONV_STOP, "ESP:CONV_STOP");
    if (ret != HAL_OK) {
        LOG_ERR("wifi conv stop: esp cmd failed: %d", ret);
        return ret;
    }

    ret = app_audio_route_release_wifi();
    if (ret != HAL_OK) {
        LOG_ERR("wifi conv stop: audio route release failed: %d", ret);
        return ret;
    }

    return HAL_OK;
}

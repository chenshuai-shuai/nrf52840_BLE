#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include "app_audio_route.h"
#include "app_esp_link.h"
#include "app_mode_manager.h"
#include "app_wifi_boot_ctrl.h"
#include "error.h"

LOG_MODULE_REGISTER(app_esp_link, LOG_LEVEL_INF);

#define WIFI_CMD_UART_NODE  DT_NODELABEL(uart1)

#if !DT_NODE_HAS_STATUS(WIFI_CMD_UART_NODE, okay)
#error "uart1 must be enabled for ESP handoff protocol"
#endif

#define ESP_LINK_STACK_SIZE      1536
#define ESP_LINK_PRIO            4
#define ESP_LINK_HOST_CMD_PRIO   5
#define ESP_LINK_RX_Q_LEN        128
#define ESP_LINK_HOST_CMD_Q_LEN  8
#define ESP_LINK_LINE_MAX_LEN    96
#define ESP_LINK_IDLE_FLUSH_MS   30
#define ESP_LINK_RSP_TIMEOUT_MS  800
#define ESP_LINK_RETRY_MAX       2

#define HOST_CMD_ENTER_DL        "NRF:ESP_DL"
#define HOST_CMD_BOOT_RUN        "NRF:ESP_BOOT"
#define HOST_CMD_RESET           "NRF:ESP_RST"
#define HOST_CMD_FW_BEGIN        "NRF:ESP_FW_BEGIN"
#define HOST_CMD_FW_END          "NRF:ESP_FW_END"
#define HOST_CMD_ESP_PING        "NRF:ESP_PING"
#define HOST_CMD_CONV_START      "NRF:CONV_START"
#define HOST_CMD_CONV_STOP       "NRF:CONV_STOP"
#define HOST_CMD_CONV_END        "NRF:CONV_END"

#define ESP_CMD_PING             "ESP_PING"
#define ESP_CMD_STATE            "STATE?"
#define ESP_CMD_AUDIO_RELEASE    "REQ_AUDIO_RELEASE"
#define ESP_CMD_AUDIO_TAKE       "REQ_AUDIO_TAKE"

#define ESP_RSP_PONG             "ESP_PONG"
#define ESP_RSP_ACK_RELEASE      "ACK_AUDIO_RELEASE"
#define ESP_RSP_ACK_TAKE         "ACK_AUDIO_TAKE"
#define ESP_RSP_STATE_PREFIX     "STATE:"
#define ESP_RSP_FAULT_PREFIX     "FAULT:"

enum esp_link_wait_type {
    ESP_LINK_WAIT_NONE = 0,
    ESP_LINK_WAIT_PING,
    ESP_LINK_WAIT_STATE,
    ESP_LINK_WAIT_AUDIO_RELEASE,
    ESP_LINK_WAIT_AUDIO_TAKE,
};

struct esp_link_ctx {
    bool inited;
    bool started;
    bool waiting;
    bool ready;
    enum esp_link_wait_type wait_type;
    int wait_result;
    app_esp_link_state_t cached_state;
    char rsp_line[ESP_LINK_LINE_MAX_LEN];
};

struct esp_host_cmd_msg {
    char line[ESP_LINK_LINE_MAX_LEN];
};

static const struct device *const g_uart = DEVICE_DT_GET(WIFI_CMD_UART_NODE);

static struct esp_link_ctx g_esp_link = {
    .cached_state = APP_ESP_STATE_UNKNOWN,
};

static struct k_thread g_esp_link_thread;
static struct k_thread g_esp_host_cmd_thread;
K_THREAD_STACK_DEFINE(g_esp_link_stack, ESP_LINK_STACK_SIZE);
K_THREAD_STACK_DEFINE(g_esp_host_cmd_stack, 1024);
K_MSGQ_DEFINE(g_esp_link_rx_q, sizeof(uint8_t), ESP_LINK_RX_Q_LEN, 1);
K_MSGQ_DEFINE(g_esp_host_cmd_q, sizeof(struct esp_host_cmd_msg), ESP_LINK_HOST_CMD_Q_LEN, 1);
K_MUTEX_DEFINE(g_esp_link_lock);
K_SEM_DEFINE(g_esp_link_rsp_sem, 0, 1);

static bool line_has_prefix(const char *line, const char *prefix)
{
    if (line == NULL || prefix == NULL) {
        return false;
    }

    return strncmp(line, prefix, strlen(prefix)) == 0;
}

static app_esp_link_state_t esp_state_from_line(const char *line)
{
    if (strcmp(line, "STATE:SAFE") == 0 || strcmp(line, "STATE:SAFE_HANDOFF") == 0) {
        return APP_ESP_STATE_SAFE_HANDOFF;
    }
    if (strcmp(line, "STATE:NRF_OWNER") == 0 || strcmp(line, "STATE:NRF_AUDIO_OWNER") == 0) {
        return APP_ESP_STATE_NRF_AUDIO_OWNER;
    }
    if (strcmp(line, "STATE:ESP_OWNER") == 0 || strcmp(line, "STATE:ESP_AUDIO_OWNER") == 0) {
        return APP_ESP_STATE_ESP_AUDIO_OWNER;
    }
    if (strcmp(line, "STATE:BOOTCTRL") == 0 || strcmp(line, "STATE:NRF_ESP_BOOTCTRL") == 0) {
        return APP_ESP_STATE_BOOTCTRL;
    }
    if (line_has_prefix(line, ESP_RSP_FAULT_PREFIX)) {
        return APP_ESP_STATE_FAULT;
    }

    return APP_ESP_STATE_UNKNOWN;
}

static bool esp_rsp_matches(enum esp_link_wait_type type, const char *line)
{
    if (line == NULL) {
        return false;
    }

    if (line_has_prefix(line, ESP_RSP_FAULT_PREFIX)) {
        return true;
    }

    switch (type) {
    case ESP_LINK_WAIT_PING:
        return strcmp(line, ESP_RSP_PONG) == 0;
    case ESP_LINK_WAIT_STATE:
        return line_has_prefix(line, ESP_RSP_STATE_PREFIX);
    case ESP_LINK_WAIT_AUDIO_RELEASE:
        return strcmp(line, ESP_RSP_ACK_RELEASE) == 0;
    case ESP_LINK_WAIT_AUDIO_TAKE:
        return strcmp(line, ESP_RSP_ACK_TAKE) == 0;
    case ESP_LINK_WAIT_NONE:
    default:
        return false;
    }
}

static int esp_uart_send_line(const char *line)
{
    if (line == NULL || line[0] == '\0') {
        return HAL_EINVAL;
    }
    if (!g_esp_link.started || !device_is_ready(g_uart)) {
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

static void esp_rsp_sem_drain(void)
{
    while (k_sem_take(&g_esp_link_rsp_sem, K_NO_WAIT) == 0) {
    }
}

static void esp_link_update_cached_state(const char *line)
{
    app_esp_link_state_t state = esp_state_from_line(line);
    if (state != APP_ESP_STATE_UNKNOWN) {
        g_esp_link.cached_state = state;
    }
}

static void esp_link_maybe_complete(const char *line)
{
    if (!g_esp_link.waiting || line == NULL) {
        return;
    }

    if (!esp_rsp_matches(g_esp_link.wait_type, line)) {
        return;
    }

    size_t n = strlen(line);
    if (n >= sizeof(g_esp_link.rsp_line)) {
        n = sizeof(g_esp_link.rsp_line) - 1U;
    }
    memcpy(g_esp_link.rsp_line, line, n);
    g_esp_link.rsp_line[n] = '\0';

    esp_link_update_cached_state(line);
    g_esp_link.ready = !line_has_prefix(line, ESP_RSP_FAULT_PREFIX);
    g_esp_link.wait_result = g_esp_link.ready ? HAL_OK : HAL_EIO;
    g_esp_link.waiting = false;
    k_sem_give(&g_esp_link_rsp_sem);
}

static int esp_link_request_sync(enum esp_link_wait_type type, const char *line)
{
    static const uint16_t retry_backoff_ms[ESP_LINK_RETRY_MAX] = { 200U, 500U };

    int lock_ret = k_mutex_lock(&g_esp_link_lock, K_MSEC(2000));
    if (lock_ret != 0) {
        return HAL_EBUSY;
    }

    int ret = HAL_EIO;
    for (size_t attempt = 0; attempt <= ESP_LINK_RETRY_MAX; attempt++) {
        esp_rsp_sem_drain();
        g_esp_link.wait_type = type;
        g_esp_link.wait_result = HAL_EBUSY;
        g_esp_link.waiting = true;
        g_esp_link.rsp_line[0] = '\0';

        ret = esp_uart_send_line(line);
        if (ret != HAL_OK) {
            g_esp_link.waiting = false;
            g_esp_link.wait_type = ESP_LINK_WAIT_NONE;
            break;
        }

        int wret = k_sem_take(&g_esp_link_rsp_sem, K_MSEC(ESP_LINK_RSP_TIMEOUT_MS));
        if (wret == 0) {
            ret = g_esp_link.wait_result;
            g_esp_link.wait_type = ESP_LINK_WAIT_NONE;
            if (ret == HAL_OK) {
                LOG_INF("esp link: %s -> %s", line, g_esp_link.rsp_line);
            } else {
                LOG_WRN("esp link fault: %s -> %s", line, g_esp_link.rsp_line);
            }
            break;
        }

        g_esp_link.waiting = false;
        g_esp_link.wait_type = ESP_LINK_WAIT_NONE;
        ret = -ETIMEDOUT;
        LOG_WRN("esp link timeout: %s (attempt %u/%u)",
                line,
                (unsigned int)(attempt + 1U),
                (unsigned int)(ESP_LINK_RETRY_MAX + 1U));

        if (attempt < ESP_LINK_RETRY_MAX) {
            k_msleep(retry_backoff_ms[attempt]);
        }
    }

    k_mutex_unlock(&g_esp_link_lock);
    return ret;
}

static void esp_link_handle_host_command(const char *cmd)
{
    if (strcmp(cmd, HOST_CMD_ENTER_DL) == 0) {
        (void)app_mode_enter_esp_fw_update();
        (void)app_wifi_boot_ctrl_enter_download();
        return;
    }
    if (strcmp(cmd, HOST_CMD_BOOT_RUN) == 0) {
        (void)app_wifi_boot_ctrl_boot_normal();
        (void)app_mode_exit_esp_fw_update();
        return;
    }
    if (strcmp(cmd, HOST_CMD_RESET) == 0) {
        (void)app_wifi_boot_ctrl_reset_only();
        return;
    }
    if (strcmp(cmd, HOST_CMD_FW_BEGIN) == 0) {
        (void)app_mode_enter_esp_fw_update();
        (void)app_wifi_boot_ctrl_enter_download();
        return;
    }
    if (strcmp(cmd, HOST_CMD_FW_END) == 0) {
        (void)app_wifi_boot_ctrl_boot_normal();
        (void)app_mode_exit_esp_fw_update();
        return;
    }
    if (strcmp(cmd, HOST_CMD_ESP_PING) == 0) {
        (void)app_esp_link_ping();
        return;
    }
    if (strcmp(cmd, HOST_CMD_CONV_START) == 0) {
        (void)app_audio_route_request_esp_audio();
        return;
    }
    if ((strcmp(cmd, HOST_CMD_CONV_STOP) == 0) || (strcmp(cmd, HOST_CMD_CONV_END) == 0)) {
        (void)app_audio_route_request_nrf_audio();
        return;
    }

    LOG_INF("esp link: unknown host cmd: %s", cmd);
}

static void esp_link_handle_line(const char *line)
{
    if (line == NULL || line[0] == '\0') {
        return;
    }

    if (line_has_prefix(line, "NRF:")) {
        struct esp_host_cmd_msg msg = {0};

        strncpy(msg.line, line, sizeof(msg.line) - 1U);
        if (k_msgq_put(&g_esp_host_cmd_q, &msg, K_NO_WAIT) != 0) {
            LOG_WRN("esp link: host cmd queue full, drop %s", line);
        }
        return;
    }

    LOG_INF("esp link rx: %s", line);
    esp_link_update_cached_state(line);
    esp_link_maybe_complete(line);
}

static void esp_host_cmd_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        struct esp_host_cmd_msg msg;
        int ret = k_msgq_get(&g_esp_host_cmd_q, &msg, K_FOREVER);
        if (ret == 0) {
            LOG_INF("esp link host line: %s", msg.line);
            esp_link_handle_host_command(msg.line);
        }
    }
}

static void esp_uart_isr(const struct device *dev, void *user_data)
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
        (void)k_msgq_put(&g_esp_link_rx_q, &buf[i], K_NO_WAIT);
    }
}

static void esp_link_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    char line[ESP_LINK_LINE_MAX_LEN];
    size_t pos = 0U;
    bool overflow = false;

    while (1) {
        uint8_t ch = 0U;
        k_timeout_t timeout = (pos > 0U || overflow) ? K_MSEC(ESP_LINK_IDLE_FLUSH_MS) : K_FOREVER;
        int ret = k_msgq_get(&g_esp_link_rx_q, &ch, timeout);
        if (ret != 0) {
            pos = 0U;
            overflow = false;
            continue;
        }

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            if (!overflow && pos > 0U && pos < sizeof(line)) {
                line[pos] = '\0';
                esp_link_handle_line(line);
            }
            pos = 0U;
            overflow = false;
            continue;
        }

        if ((ch < 0x20U) || (ch > 0x7eU)) {
            pos = 0U;
            overflow = false;
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

int app_esp_link_init(void)
{
    g_esp_link.inited = true;
    return HAL_OK;
}

int app_esp_link_start(void)
{
    if (g_esp_link.started) {
        return HAL_OK;
    }
    if (!device_is_ready(g_uart)) {
        LOG_ERR("esp link: uart1 not ready");
        return HAL_ENODEV;
    }

    uart_irq_callback_user_data_set(g_uart, esp_uart_isr, NULL);
    uart_irq_rx_enable(g_uart);

    k_thread_create(&g_esp_link_thread,
                    g_esp_link_stack,
                    K_THREAD_STACK_SIZEOF(g_esp_link_stack),
                    esp_link_thread_entry,
                    NULL, NULL, NULL,
                    ESP_LINK_PRIO,
                    0,
                    K_NO_WAIT);
    k_thread_name_set(&g_esp_link_thread, "esp_link");

    k_thread_create(&g_esp_host_cmd_thread,
                    g_esp_host_cmd_stack,
                    K_THREAD_STACK_SIZEOF(g_esp_host_cmd_stack),
                    esp_host_cmd_thread_entry,
                    NULL, NULL, NULL,
                    ESP_LINK_HOST_CMD_PRIO,
                    0,
                    K_NO_WAIT);
    k_thread_name_set(&g_esp_host_cmd_thread, "esp_link_host");

    g_esp_link.started = true;
    LOG_INF("esp link: started");
    return HAL_OK;
}

bool app_esp_link_is_started(void)
{
    return g_esp_link.started;
}

int app_esp_link_ping(void)
{
    return esp_link_request_sync(ESP_LINK_WAIT_PING, ESP_CMD_PING);
}

bool app_esp_link_is_ready(void)
{
    return g_esp_link.ready;
}

int app_esp_link_query_state(app_esp_link_state_t *state)
{
    int ret = esp_link_request_sync(ESP_LINK_WAIT_STATE, ESP_CMD_STATE);
    if (ret != HAL_OK) {
        return ret;
    }

    if (state != NULL) {
        *state = g_esp_link.cached_state;
    }
    return HAL_OK;
}

app_esp_link_state_t app_esp_link_cached_state(void)
{
    return g_esp_link.cached_state;
}

int app_esp_link_request_audio_release(void)
{
    return esp_link_request_sync(ESP_LINK_WAIT_AUDIO_RELEASE, ESP_CMD_AUDIO_RELEASE);
}

int app_esp_link_request_audio_take(void)
{
    return esp_link_request_sync(ESP_LINK_WAIT_AUDIO_TAKE, ESP_CMD_AUDIO_TAKE);
}

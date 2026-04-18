#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_mode_manager.h"
#include "app_quiet_manager.h"
#include "error.h"

LOG_MODULE_REGISTER(app_mode_manager, LOG_LEVEL_INF);

static app_mode_t g_app_mode = APP_MODE_NORMAL;
K_MUTEX_DEFINE(g_app_mode_lock);

void app_mode_manager_init(void)
{
    k_mutex_lock(&g_app_mode_lock, K_FOREVER);
    g_app_mode = APP_MODE_NORMAL;
    k_mutex_unlock(&g_app_mode_lock);
    app_quiet_manager_init();
}

app_mode_t app_mode_get(void)
{
    app_mode_t mode;

    k_mutex_lock(&g_app_mode_lock, K_FOREVER);
    mode = g_app_mode;
    k_mutex_unlock(&g_app_mode_lock);
    return mode;
}

bool app_mode_is_esp_fw_update(void)
{
    return app_mode_get() == APP_MODE_ESP_FW_UPDATE;
}

int app_mode_enter_esp_fw_update(void)
{
    int ret = HAL_OK;

    k_mutex_lock(&g_app_mode_lock, K_FOREVER);
    if (g_app_mode == APP_MODE_ESP_FW_UPDATE) {
        k_mutex_unlock(&g_app_mode_lock);
        return HAL_OK;
    }
    k_mutex_unlock(&g_app_mode_lock);

    ret = app_quiet_manager_enter_update();
    if (ret != HAL_OK) {
        LOG_ERR("mode enter update: quiet failed: %d", ret);
        return ret;
    }

    k_mutex_lock(&g_app_mode_lock, K_FOREVER);
    g_app_mode = APP_MODE_ESP_FW_UPDATE;
    k_mutex_unlock(&g_app_mode_lock);
    LOG_INF("mode -> ESP_FW_UPDATE");
    return HAL_OK;
}

int app_mode_exit_esp_fw_update(void)
{
    int ret = HAL_OK;

    k_mutex_lock(&g_app_mode_lock, K_FOREVER);
    if (g_app_mode == APP_MODE_NORMAL) {
        k_mutex_unlock(&g_app_mode_lock);
        return HAL_OK;
    }
    k_mutex_unlock(&g_app_mode_lock);

    ret = app_quiet_manager_exit_update();
    if (ret != HAL_OK) {
        LOG_ERR("mode exit update: resume failed: %d", ret);
        return ret;
    }

    k_mutex_lock(&g_app_mode_lock, K_FOREVER);
    g_app_mode = APP_MODE_NORMAL;
    k_mutex_unlock(&g_app_mode_lock);
    LOG_INF("mode -> NORMAL");
    return HAL_OK;
}

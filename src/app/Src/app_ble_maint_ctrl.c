#include <zephyr/logging/log.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "app_ble_maint_ctrl.h"
#include "app_mode_manager.h"
#include "app_wifi_boot_ctrl.h"
#include "error.h"

LOG_MODULE_REGISTER(app_ble_maint_ctrl, LOG_LEVEL_INF);

int app_ble_maint_ctrl_init(void)
{
    return HAL_OK;
}

bool app_ble_maint_ctrl_handle_message(const uint8_t *buf, int len)
{
    if (buf == NULL || len <= 0) {
        return false;
    }

    if (len == 10 && memcmp(buf, "NRF:ESP_DL", 10) == 0) {
        LOG_INF("BLE CTRL RX: NRF:ESP_DL");
        LOG_INF("esp link host line: NRF:ESP_DL");
        (void)app_mode_enter_esp_fw_update();
        int ret = app_wifi_boot_ctrl_enter_download();
        if (ret != HAL_OK) {
            LOG_WRN("ble maint: NRF:ESP_DL failed: %d", ret);
        }
        return true;
    }

    if (len == 12 && memcmp(buf, "NRF:ESP_BOOT", 12) == 0) {
        LOG_INF("BLE CTRL RX: NRF:ESP_BOOT");
        LOG_INF("esp link host line: NRF:ESP_BOOT");
        int ret = app_wifi_boot_ctrl_boot_normal();
        (void)app_mode_exit_esp_fw_update();
        if (ret != HAL_OK) {
            LOG_WRN("ble maint: NRF:ESP_BOOT failed: %d", ret);
        }
        return true;
    }

    return false;
}

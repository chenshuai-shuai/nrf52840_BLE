#include <stdbool.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include "platform_init.h"
#include "platform_registry.h"
#include "platform_caps.h"
#include "driver_audio_nrf.h"
#include "driver_ppg_nrf.h"
#include "driver_imu_nrf.h"
#include "driver_flash_nrf.h"
#include "driver_gps_nrf.h"
#include "driver_ble_nrf.h"
#include "driver_mic_nrf.h"
#include "driver_spk_nrf.h"
#include "driver_pm_nrf.h"
#include "error.h"

#define PLATFORM_NAME_DEFAULT "nrf52840"

LOG_MODULE_REGISTER(platform_init, LOG_LEVEL_WRN);

static int platform_nrf52840_init(void)
{
    int ret;
    LOG_INF("platform: register drivers");

    if (IS_ENABLED(CONFIG_DRIVER_AUDIO_NRF)) {
        LOG_INF("platform: audio_nrf_register");
        ret = audio_nrf_register();
        if (ret != HAL_OK) {
            LOG_ERR("platform: audio_nrf_register failed: %d", ret);
            return ret;
        }
    }
    if (IS_ENABLED(CONFIG_DRIVER_PPG_NRF)) {
        LOG_INF("platform: ppg_nrf_register");
        ret = ppg_nrf_register();
        if (ret != HAL_OK) {
            LOG_ERR("platform: ppg_nrf_register failed: %d", ret);
            return ret;
        }
    }
    if (IS_ENABLED(CONFIG_DRIVER_IMU_NRF)) {
        LOG_INF("platform: imu_nrf_register");
        ret = imu_nrf_register();
        if (ret != HAL_OK) {
            LOG_ERR("platform: imu_nrf_register failed: %d", ret);
            return ret;
        }
    }
    if (IS_ENABLED(CONFIG_DRIVER_FLASH_NRF)) {
        LOG_INF("platform: flash_nrf_register");
        ret = flash_nrf_register();
        if (ret != HAL_OK) {
            LOG_ERR("platform: flash_nrf_register failed: %d", ret);
            return ret;
        }
    }
    if (IS_ENABLED(CONFIG_DRIVER_GPS_NRF)) {
        LOG_INF("platform: gps_nrf_register");
        ret = gps_nrf_register();
        if (ret != HAL_OK) {
            LOG_ERR("platform: gps_nrf_register failed: %d", ret);
            return ret;
        }
    }
    if (IS_ENABLED(CONFIG_DRIVER_BLE_NRF)) {
        LOG_INF("platform: ble_nrf_register");
        ret = ble_nrf_register();
        if (ret != HAL_OK) {
            LOG_ERR("platform: ble_nrf_register failed: %d", ret);
            return ret;
        }
    }
    if (IS_ENABLED(CONFIG_DRIVER_MIC_NRF)) {
        LOG_INF("platform: mic_nrf_register");
        ret = mic_nrf_register();
        if (ret != HAL_OK) {
            LOG_ERR("platform: mic_nrf_register failed: %d", ret);
            return ret;
        }
    }
    if (IS_ENABLED(CONFIG_DRIVER_SPK_NRF)) {
        LOG_INF("platform: spk_nrf_register");
        ret = spk_nrf_register();
        if (ret != HAL_OK) {
            LOG_ERR("platform: spk_nrf_register failed: %d", ret);
            return ret;
        }
    }
    if (IS_ENABLED(CONFIG_DRIVER_PM_NRF)) {
        LOG_INF("platform: pm_nrf_register");
        ret = pm_nrf_register();
        if (ret != HAL_OK) {
            LOG_ERR("platform: pm_nrf_register failed: %d", ret);
            return ret;
        }
    }

    platform_caps_t caps = {
        .audio = IS_ENABLED(CONFIG_DRIVER_AUDIO_NRF),
        .ppg = IS_ENABLED(CONFIG_DRIVER_PPG_NRF),
        .imu = IS_ENABLED(CONFIG_DRIVER_IMU_NRF),
        .flash = IS_ENABLED(CONFIG_DRIVER_FLASH_NRF),
        .gps = IS_ENABLED(CONFIG_DRIVER_GPS_NRF),
        .ble = IS_ENABLED(CONFIG_DRIVER_BLE_NRF),
        .mic = IS_ENABLED(CONFIG_DRIVER_MIC_NRF),
        .spk = IS_ENABLED(CONFIG_DRIVER_SPK_NRF),
        .pm = IS_ENABLED(CONFIG_DRIVER_PM_NRF),
    };

    ret = platform_caps_set(&caps);
    if (ret != HAL_OK) {
        return ret;
    }

    return HAL_OK;
}

int platform_init(void)
{
    static bool g_platform_inited;
    if (g_platform_inited) {
        LOG_INF("platform: already inited");
        return HAL_OK;
    }

#if !IS_ENABLED(CONFIG_PPG_TUNE_MODE)
    printk("platform: init start\n");
#endif
    int ret = platform_register(PLATFORM_NAME_DEFAULT, platform_nrf52840_init);
    if (ret != HAL_OK && ret != HAL_EBUSY) {
        LOG_ERR("platform: register failed: %d", ret);
        return ret;
    }

    ret = platform_set_active(PLATFORM_NAME_DEFAULT);
    if (ret != HAL_OK) {
        LOG_ERR("platform: set_active failed: %d", ret);
        return ret;
    }

    ret = platform_init_selected();
    if (ret == HAL_OK) {
        g_platform_inited = true;
        LOG_INF("platform: init done");
#if !IS_ENABLED(CONFIG_PPG_TUNE_MODE)
        printk("platform: init done\n");
#endif
    } else {
        LOG_ERR("platform: init failed: %d", ret);
        printk("platform: init failed: %d\n", ret);
    }
    return ret;
}

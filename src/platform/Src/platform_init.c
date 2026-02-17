#include <stdbool.h>
#include <zephyr/sys/util.h>

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

static int platform_nrf52840_init(void)
{
    int ret;

    if (IS_ENABLED(CONFIG_DRIVER_AUDIO_NRF)) {
        ret = audio_nrf_register();
        if (ret != HAL_OK) {
            return ret;
        }
    }
    if (IS_ENABLED(CONFIG_DRIVER_PPG_NRF)) {
        ret = ppg_nrf_register();
        if (ret != HAL_OK) {
            return ret;
        }
    }
    if (IS_ENABLED(CONFIG_DRIVER_IMU_NRF)) {
        ret = imu_nrf_register();
        if (ret != HAL_OK) {
            return ret;
        }
    }
    if (IS_ENABLED(CONFIG_DRIVER_FLASH_NRF)) {
        ret = flash_nrf_register();
        if (ret != HAL_OK) {
            return ret;
        }
    }
    if (IS_ENABLED(CONFIG_DRIVER_GPS_NRF)) {
        ret = gps_nrf_register();
        if (ret != HAL_OK) {
            return ret;
        }
    }
    if (IS_ENABLED(CONFIG_DRIVER_BLE_NRF)) {
        ret = ble_nrf_register();
        if (ret != HAL_OK) {
            return ret;
        }
    }
    if (IS_ENABLED(CONFIG_DRIVER_MIC_NRF)) {
        ret = mic_nrf_register();
        if (ret != HAL_OK) {
            return ret;
        }
    }
    if (IS_ENABLED(CONFIG_DRIVER_SPK_NRF)) {
        ret = spk_nrf_register();
        if (ret != HAL_OK) {
            return ret;
        }
    }
    if (IS_ENABLED(CONFIG_DRIVER_PM_NRF)) {
        ret = pm_nrf_register();
        if (ret != HAL_OK) {
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
        return HAL_OK;
    }

    int ret = platform_register(PLATFORM_NAME_DEFAULT, platform_nrf52840_init);
    if (ret != HAL_OK && ret != HAL_EBUSY) {
        return ret;
    }

    ret = platform_set_active(PLATFORM_NAME_DEFAULT);
    if (ret != HAL_OK) {
        return ret;
    }

    ret = platform_init_selected();
    if (ret == HAL_OK) {
        g_platform_inited = true;
    }
    return ret;
}

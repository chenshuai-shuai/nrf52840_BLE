#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_quiet_manager.h"
#include "app_uplink_service.h"
#include "error.h"

#if IS_ENABLED(CONFIG_PPG_SPI_PROBE)
#include "app_ppg_hr.h"
#endif

#if IS_ENABLED(CONFIG_IMU_TEST)
#include "app_imu_test.h"
#endif

#if IS_ENABLED(CONFIG_GPS_TEST)
#include "app_gps.h"
#endif

LOG_MODULE_REGISTER(app_quiet_manager, LOG_LEVEL_INF);

typedef struct {
    bool quieted;
    bool uplink_paused;
    bool ppg_paused;
    bool imu_paused;
    bool gps_paused;
} app_quiet_state_t;

static app_quiet_state_t g_quiet_state;
K_MUTEX_DEFINE(g_quiet_lock);

void app_quiet_manager_init(void)
{
    k_mutex_lock(&g_quiet_lock, K_FOREVER);
    g_quiet_state = (app_quiet_state_t){0};
    k_mutex_unlock(&g_quiet_lock);
}

int app_quiet_manager_enter_update(void)
{
    int ret = HAL_OK;

    k_mutex_lock(&g_quiet_lock, K_FOREVER);
    if (g_quiet_state.quieted) {
        k_mutex_unlock(&g_quiet_lock);
        return HAL_OK;
    }

    ret = app_uplink_service_pause();
    if (ret == HAL_OK) {
        g_quiet_state.uplink_paused = true;
    } else {
        LOG_WRN("quiet: uplink pause failed: %d", ret);
    }

#if IS_ENABLED(CONFIG_PPG_SPI_PROBE)
    app_ppg_hr_pause();
    g_quiet_state.ppg_paused = true;
#endif

#if IS_ENABLED(CONFIG_IMU_TEST)
    app_imu_test_pause();
    g_quiet_state.imu_paused = true;
#endif

#if IS_ENABLED(CONFIG_GPS_TEST)
    app_gps_pause();
    g_quiet_state.gps_paused = true;
#endif

    g_quiet_state.quieted = true;
    k_mutex_unlock(&g_quiet_lock);
    LOG_INF("quiet: business apps paused for ESP FW update");
    return HAL_OK;
}

int app_quiet_manager_exit_update(void)
{
    int ret = HAL_OK;

    k_mutex_lock(&g_quiet_lock, K_FOREVER);
    if (!g_quiet_state.quieted) {
        k_mutex_unlock(&g_quiet_lock);
        return HAL_OK;
    }

#if IS_ENABLED(CONFIG_GPS_TEST)
    if (g_quiet_state.gps_paused) {
        app_gps_resume();
        g_quiet_state.gps_paused = false;
    }
#endif

#if IS_ENABLED(CONFIG_IMU_TEST)
    if (g_quiet_state.imu_paused) {
        app_imu_test_resume();
        g_quiet_state.imu_paused = false;
    }
#endif

#if IS_ENABLED(CONFIG_PPG_SPI_PROBE)
    if (g_quiet_state.ppg_paused) {
        app_ppg_hr_resume();
        g_quiet_state.ppg_paused = false;
    }
#endif

    if (g_quiet_state.uplink_paused) {
        ret = app_uplink_service_resume();
        if (ret != HAL_OK) {
            LOG_WRN("quiet: uplink resume failed: %d", ret);
        }
        g_quiet_state.uplink_paused = false;
    }

    g_quiet_state.quieted = false;
    k_mutex_unlock(&g_quiet_lock);
    LOG_INF("quiet: business apps resumed");
    return HAL_OK;
}

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#include "app_lifecycle.h"
#include "error.h"
#include "hal_audio.h"
#include "hal_pm.h"
#include "hal_ppg.h"
#include "app_rtc.h"
#include "app_imu_test.h"
#include "app_gps.h"
#include "app_ppg_hr.h"
#include "app_pm_test.h"
#include "pm_service.h"
#include "app_bus.h"
#include "app_uplink_service.h"

LOG_MODULE_REGISTER(app_lifecycle, LOG_LEVEL_INF);

typedef int (*app_start_fn_t)(void);
typedef int (*app_stop_fn_t)(void);
typedef bool (*app_ready_fn_t)(void);

typedef struct {
    const char *name;
    bool configured;
    bool enabled;
    app_start_fn_t start;
    app_stop_fn_t stop;
    app_ready_fn_t ready;
    app_lifecycle_status_t st;
} app_lifecycle_entry_t;

typedef struct {
    app_lifecycle_id_t id;
    bool required;
    uint32_t deps_mask;
} app_boot_item_t;

static int pm_start(void)
{
#if IS_ENABLED(CONFIG_PM_SERVICE)
    int ret = pm_service_start();
    if (ret != HAL_OK) {
        return ret;
    }
    for (int i = 0; i < 40; i++) {
        if (pm_service_is_ready()) {
            return HAL_OK;
        }
        k_msleep(50);
    }
    {
        int last = pm_service_last_error();
        return (last != 0) ? last : HAL_EIO;
    }
#elif IS_ENABLED(CONFIG_HAL_PM) && IS_ENABLED(CONFIG_DRIVER_PM_NRF)
    int ret = hal_pm_init();
    if (ret != HAL_OK) {
        return ret;
    }
    return hal_pm_set_mode(HAL_PM_MODE_DEFAULT);
#else
    return HAL_ENOTSUP;
#endif
}

static int pm_stop(void)
{
    return HAL_ENOTSUP;
}

static bool pm_ready(void)
{
#if IS_ENABLED(CONFIG_PM_SERVICE)
    return pm_service_is_ready();
#else
    return true;
#endif
}

static int rtc_start(void)
{
#if IS_ENABLED(CONFIG_APP_RTC_ENABLE)
    int ret = hal_audio_init();
    if (ret != HAL_OK) {
        return ret;
    }
    app_rtc_init();
    app_rtc_start();
    return HAL_OK;
#else
    return HAL_ENOTSUP;
#endif
}

static int uplink_start(void)
{
#if IS_ENABLED(CONFIG_HAL_BLE) && IS_ENABLED(CONFIG_DRIVER_BLE_NRF) && IS_ENABLED(CONFIG_BT)
    return app_uplink_service_start();
#else
    return HAL_ENOTSUP;
#endif
}

static int uplink_stop(void)
{
    return HAL_ENOTSUP;
}

static bool uplink_ready(void)
{
    return app_uplink_service_is_ready();
}

static int rtc_stop(void)
{
    return HAL_ENOTSUP;
}

static bool rtc_ready(void)
{
    return true;
}

static int imu_start(void)
{
#if IS_ENABLED(CONFIG_IMU_TEST)
    app_imu_test_start();
    return HAL_OK;
#else
    return HAL_ENOTSUP;
#endif
}

static int imu_stop(void)
{
    return HAL_ENOTSUP;
}

static bool imu_ready(void)
{
    return true;
}

static int ppg_start(void)
{
#if IS_ENABLED(CONFIG_PPG_SPI_PROBE)
    return app_ppg_hr_start();
#else
    return HAL_ENOTSUP;
#endif
}

static int ppg_stop(void)
{
#if IS_ENABLED(CONFIG_PPG_SPI_PROBE)
    return hal_ppg_stop();
#else
    return HAL_ENOTSUP;
#endif
}

static bool ppg_ready(void)
{
    return true;
}

static int gps_start(void)
{
#if IS_ENABLED(CONFIG_GPS_TEST)
    app_gps_start();
    return HAL_OK;
#else
    return HAL_ENOTSUP;
#endif
}

static int gps_stop(void)
{
    return HAL_ENOTSUP;
}

static bool gps_ready(void)
{
    return true;
}

static int pm_test_start(void)
{
#if IS_ENABLED(CONFIG_PM_TEST)
    app_pm_test_start();
    return HAL_OK;
#else
    return HAL_ENOTSUP;
#endif
}

static int pm_test_stop(void)
{
    return HAL_ENOTSUP;
}

static bool pm_test_ready(void)
{
    return true;
}

static app_lifecycle_entry_t g_apps[APP_LC_COUNT] = {
    [APP_LC_PM] = {
        .name = "pm",
        .configured = IS_ENABLED(CONFIG_PM_SERVICE) ||
                      (IS_ENABLED(CONFIG_HAL_PM) && IS_ENABLED(CONFIG_DRIVER_PM_NRF)),
        .enabled = true,
        .start = pm_start,
        .stop = pm_stop,
        .ready = pm_ready,
        .st = {.configured = false, .started = false, .ready = false, .last_error = HAL_OK}
    },
    [APP_LC_RTC] = {
        .name = "rtc",
        .configured = IS_ENABLED(CONFIG_APP_RTC_ENABLE),
        .enabled = IS_ENABLED(CONFIG_APP_RTC_ENABLE),
        .start = rtc_start,
        .stop = rtc_stop,
        .ready = rtc_ready,
        .st = {.configured = false, .started = false, .ready = false, .last_error = HAL_OK}
    },
    [APP_LC_UPLINK] = {
        .name = "uplink",
        .configured = IS_ENABLED(CONFIG_HAL_BLE) &&
                      IS_ENABLED(CONFIG_DRIVER_BLE_NRF) &&
                      IS_ENABLED(CONFIG_BT),
        .enabled = IS_ENABLED(CONFIG_HAL_BLE) &&
                   IS_ENABLED(CONFIG_DRIVER_BLE_NRF) &&
                   IS_ENABLED(CONFIG_BT),
        .start = uplink_start,
        .stop = uplink_stop,
        .ready = uplink_ready,
        .st = {.configured = false, .started = false, .ready = false, .last_error = HAL_OK}
    },
    [APP_LC_IMU] = {
        .name = "imu",
        .configured = IS_ENABLED(CONFIG_IMU_TEST),
        .enabled = IS_ENABLED(CONFIG_IMU_TEST),
        .start = imu_start,
        .stop = imu_stop,
        .ready = imu_ready,
        .st = {.configured = false, .started = false, .ready = false, .last_error = HAL_OK}
    },
    [APP_LC_PPG] = {
        .name = "ppg",
        .configured = IS_ENABLED(CONFIG_PPG_SPI_PROBE),
        .enabled = IS_ENABLED(CONFIG_PPG_SPI_PROBE),
        .start = ppg_start,
        .stop = ppg_stop,
        .ready = ppg_ready,
        .st = {.configured = false, .started = false, .ready = false, .last_error = HAL_OK}
    },
    [APP_LC_GPS] = {
        .name = "gps",
        .configured = IS_ENABLED(CONFIG_GPS_TEST),
        .enabled = IS_ENABLED(CONFIG_GPS_TEST),
        .start = gps_start,
        .stop = gps_stop,
        .ready = gps_ready,
        .st = {.configured = false, .started = false, .ready = false, .last_error = HAL_OK}
    },
    [APP_LC_PM_TEST] = {
        .name = "pm_test",
        .configured = IS_ENABLED(CONFIG_PM_TEST),
        .enabled = IS_ENABLED(CONFIG_PM_TEST),
        .start = pm_test_start,
        .stop = pm_test_stop,
        .ready = pm_test_ready,
        .st = {.configured = false, .started = false, .ready = false, .last_error = HAL_OK}
    },
};

static const app_boot_item_t g_boot_order[] = {
    {.id = APP_LC_PM, .required = true,  .deps_mask = 0},
    {.id = APP_LC_UPLINK, .required = false, .deps_mask = BIT(APP_LC_PM)},
    {.id = APP_LC_RTC, .required = false, .deps_mask = BIT(APP_LC_UPLINK)},
    {.id = APP_LC_IMU, .required = false, .deps_mask = 0},
    {.id = APP_LC_PPG, .required = false, .deps_mask = BIT(APP_LC_PM)},
    {.id = APP_LC_GPS, .required = false, .deps_mask = BIT(APP_LC_PM)},
    {.id = APP_LC_PM_TEST, .required = false, .deps_mask = BIT(APP_LC_PM)},
};

const char *app_lifecycle_name(app_lifecycle_id_t id)
{
    if (id < 0 || id >= APP_LC_COUNT) {
        return "unknown";
    }
    return g_apps[id].name;
}

bool app_lifecycle_is_enabled(app_lifecycle_id_t id)
{
    if (id < 0 || id >= APP_LC_COUNT) {
        return false;
    }
    return g_apps[id].enabled;
}

int app_lifecycle_set_enabled(app_lifecycle_id_t id, bool enabled)
{
    if (id < 0 || id >= APP_LC_COUNT) {
        return HAL_EINVAL;
    }
    g_apps[id].enabled = enabled;
    return HAL_OK;
}

int app_lifecycle_start(app_lifecycle_id_t id)
{
    if (id < 0 || id >= APP_LC_COUNT) {
        return HAL_EINVAL;
    }

    app_lifecycle_entry_t *e = &g_apps[id];
    e->st.configured = e->configured;

    if (!e->enabled) {
        e->st.last_error = HAL_ENOTSUP;
        return HAL_ENOTSUP;
    }

    if (!e->configured) {
        e->st.last_error = HAL_ENOTSUP;
        return HAL_ENOTSUP;
    }

    if (e->st.started) {
        return HAL_OK;
    }

    int ret = e->start ? e->start() : HAL_ENOTSUP;
    e->st.last_error = ret;
    if (ret != HAL_OK) {
        e->st.started = false;
        e->st.ready = false;
        LOG_ERR("app_lc start failed: %s ret=%d", e->name, ret);
        return ret;
    }

    e->st.started = true;
    e->st.ready = e->ready ? e->ready() : true;
    app_event_t evt = {
        .id = APP_EVT_APP_LIFECYCLE,
        .timestamp_ms = (uint32_t)k_uptime_get(),
        .data.lifecycle = {
            .app_id = id,
            .status = e->st,
        },
    };
    (void)app_bus_publish(&evt);
    LOG_INF("app_lc started: %s ready=%d", e->name, e->st.ready ? 1 : 0);
    return HAL_OK;
}

int app_lifecycle_stop(app_lifecycle_id_t id)
{
    if (id < 0 || id >= APP_LC_COUNT) {
        return HAL_EINVAL;
    }

    app_lifecycle_entry_t *e = &g_apps[id];
    if (!e->st.started) {
        return HAL_OK;
    }

    int ret = e->stop ? e->stop() : HAL_ENOTSUP;
    e->st.last_error = ret;
    if (ret == HAL_OK) {
        e->st.started = false;
        e->st.ready = false;
        app_event_t evt = {
            .id = APP_EVT_APP_LIFECYCLE,
            .timestamp_ms = (uint32_t)k_uptime_get(),
            .data.lifecycle = {
                .app_id = id,
                .status = e->st,
            },
        };
        (void)app_bus_publish(&evt);
    }
    return ret;
}

int app_lifecycle_start_all(void)
{
    uint32_t started_mask = 0;

    for (size_t i = 0; i < ARRAY_SIZE(g_boot_order); i++) {
        const app_boot_item_t *it = &g_boot_order[i];
        app_lifecycle_entry_t *e = &g_apps[it->id];

        if (!e->configured) {
            continue;
        }
        if (!e->enabled && !it->required) {
            continue;
        }
        if ((it->deps_mask & started_mask) != it->deps_mask) {
            LOG_ERR("app_lc dep missing: %s deps=0x%08x started=0x%08x",
                    e->name, (unsigned int)it->deps_mask, (unsigned int)started_mask);
            return HAL_EBUSY;
        }

        int ret = app_lifecycle_start(it->id);
        if (ret != HAL_OK) {
            if (it->required) {
                return ret;
            }
            LOG_WRN("app_lc optional start skipped: %s ret=%d", e->name, ret);
            continue;
        }
        started_mask |= BIT(it->id);
    }

    return HAL_OK;
}

int app_lifecycle_stop_all(void)
{
    for (int i = (int)ARRAY_SIZE(g_boot_order) - 1; i >= 0; i--) {
        const app_boot_item_t *it = &g_boot_order[i];
        app_lifecycle_entry_t *e = &g_apps[it->id];
        if (!e->st.started) {
            continue;
        }
        int ret = app_lifecycle_stop(it->id);
        if (ret != HAL_OK && ret != HAL_ENOTSUP) {
            LOG_WRN("app_lc stop failed: %s ret=%d", e->name, ret);
        }
    }
    return HAL_OK;
}

int app_lifecycle_get_status(app_lifecycle_id_t id, app_lifecycle_status_t *out)
{
    if (id < 0 || id >= APP_LC_COUNT || out == NULL) {
        return HAL_EINVAL;
    }

    app_lifecycle_entry_t *e = &g_apps[id];
    e->st.configured = e->configured;
    if (e->st.started && e->ready) {
        e->st.ready = e->ready();
    }
    *out = e->st;
    return HAL_OK;
}

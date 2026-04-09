#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>

#include "platform_init.h"
#include "error.h"

#include "app_lifecycle.h"
#include "app_bus.h"
#include "boot_tone.h"
#include "system_state.h"
#include "app_db.h"
#include "spi_bus_arbiter.h"
#include "app_spk_diag.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_main, LOG_LEVEL_WRN);

int main(void)
{
    int ret = platform_init();
    if (ret != HAL_OK) {
        printk("platform_init failed: %d\n", ret);
        return ret;
    }

    system_state_init();
    (void)app_db_init();
    (void)spi_bus_arbiter_init();
    ret = app_bus_start();
    if (ret != HAL_OK) {
        printk("app_bus_start failed: %d\n", ret);
        return ret;
    }

#if IS_ENABLED(CONFIG_SPK_TEST)
    /* Speaker isolated test mode: do not start PM/MIC/BLE/PPG/IMU apps. */
    (void)app_spk_diag_start();
    printk("spk_test isolated mode\n");
    return 0;
#endif

#if IS_ENABLED(CONFIG_TEMP_TEST_ISOLATED)
    /* Temperature isolated test mode: only bring up PM and TMP119. */
    ret = app_lifecycle_start(APP_LC_PM);
    if (ret != HAL_OK) {
        printk("temp_test pm start failed: %d\n", ret);
        return ret;
    }

    ret = app_lifecycle_start(APP_LC_TEMP);
    if (ret != HAL_OK) {
        printk("temp_test start failed: %d\n", ret);
        return ret;
    }

    printk("temp_test isolated mode\n");
    return 0;
#endif

#if IS_ENABLED(CONFIG_BOOT_TONE)
    /* Bring up PM first so speaker/amp rails are stable before boot tone. */
    (void)app_lifecycle_start(APP_LC_PM);
    boot_tone_play();
#endif

    ret = app_lifecycle_start_all();
    if (ret != HAL_OK) {
        printk("app lifecycle start_all failed: %d\n", ret);
        return ret;
    }

#if IS_ENABLED(CONFIG_SPK_TEST)
    (void)app_spk_diag_start();
#endif

    app_lifecycle_status_t ppg_st = {0};
    if (app_lifecycle_get_status(APP_LC_PPG, &ppg_st) == HAL_OK && ppg_st.started) {
#if !IS_ENABLED(CONFIG_PPG_TUNE_MODE)
        printk("gh3026 run start\n");
#endif
    }

    /* Hardware watchdog: auto-reset if main loop stalls */
#if IS_ENABLED(CONFIG_WATCHDOG)
    const struct device *const wdt = DEVICE_DT_GET(DT_NODELABEL(wdt0));
    if (device_is_ready(wdt)) {
        struct wdt_timeout_cfg wdt_cfg = {
            .window.min = 0U,
            .window.max = 8000U, /* 8 second timeout */
            .callback = NULL,    /* system reset on timeout */
            .flags = WDT_FLAG_RESET_SOC,
        };
        int wdt_channel = wdt_install_timeout(wdt, &wdt_cfg);
        if (wdt_channel >= 0) {
            int wdt_ret = wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
            if (wdt_ret == 0) {
                LOG_INF("watchdog started (8s timeout, channel %d)", wdt_channel);
                /* Keep main thread alive to feed the watchdog */
                while (1) {
                    wdt_feed(wdt, wdt_channel);
                    k_sleep(K_MSEC(4000));
                }
            } else {
                LOG_ERR("watchdog setup failed: %d", wdt_ret);
            }
        } else {
            LOG_ERR("watchdog install timeout failed: %d", wdt_channel);
        }
    } else {
        LOG_WRN("watchdog device not ready");
    }
#endif

    return 0;
}

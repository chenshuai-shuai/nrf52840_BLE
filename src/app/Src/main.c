#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

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
        printk("gh3026 run start\n");
    }

    return 0;
}

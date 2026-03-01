#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "platform_init.h"
#include "hal_audio.h"
#include "hal_pm.h"
#include "error.h"

#include "app_rtc.h"
#include "app_state.h"
#include "app_imu_test.h"
#include "app_pm_test.h"
#include "app_ppg_hr.h"
#include "boot_tone.h"
#include "system_state.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_main, LOG_LEVEL_INF);

int main(void)
{
    int ret = platform_init();
    if (ret != HAL_OK) {
        printk("platform_init failed: %d\n", ret);
        return ret;
    }

    system_state_init();

#if IS_ENABLED(CONFIG_BOOT_TONE)
    boot_tone_play();
#endif

#if IS_ENABLED(CONFIG_APP_RTC_ENABLE)
    ret = hal_audio_init();
    if (ret != HAL_OK) {
        printk("hal_audio_init failed: %d\n", ret);
        return ret;
    }

    app_rtc_init();
    app_rtc_start();
#else
    LOG_INF("app_rtc disabled by config");
#endif

#if IS_ENABLED(CONFIG_IMU_TEST)
    printk("imu_test: start\n");
    app_imu_test_start();
#endif

#if IS_ENABLED(CONFIG_PM_SERVICE)
    printk("pm_service: start\n");
    app_pm_test_start();
    /* PM service thread will configure PMIC and keep it running in background. */
    k_msleep(300);
#endif

#if IS_ENABLED(CONFIG_HAL_PM) && IS_ENABLED(CONFIG_DRIVER_PM_NRF)
#if !IS_ENABLED(CONFIG_PM_SERVICE)
    ret = hal_pm_init();
    if (ret != HAL_OK) {
        printk("pm init failed: %d\n", ret);
        return ret;
    }
    ret = hal_pm_set_mode(HAL_PM_MODE_DEFAULT);
    if (ret != HAL_OK) {
        printk("pm default mode failed: %d\n", ret);
        return ret;
    }
    printk("pm default mode set (sbb 3v3 enabled)\n");
#endif
#endif

#if IS_ENABLED(CONFIG_PPG_SPI_PROBE)
    ret = app_ppg_hr_start();
    if (ret != HAL_OK) {
        printk("ppg hr app start failed: %d\n", ret);
        return ret;
    }
    printk("gh3026 run start\n");
#endif

#if IS_ENABLED(CONFIG_PM_TEST)
    printk("pm_test: start\n");
    app_pm_test_start();
#endif

    return 0;
}

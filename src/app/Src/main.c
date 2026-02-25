#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "platform_init.h"
#include "hal_audio.h"
#include "error.h"

#include "app_rtc.h"
#include "app_state.h"
#include "app_imu_test.h"
#include "boot_tone.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_main, LOG_LEVEL_INF);

int main(void)
{
    int ret = platform_init();
    if (ret != HAL_OK) {
        printk("platform_init failed: %d\n", ret);
        return ret;
    }

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

    return 0;
}

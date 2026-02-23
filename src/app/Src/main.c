#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "platform_init.h"
#include "hal_audio.h"
#include "error.h"

#include "app_rtc.h"
#include "app_state.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_main, LOG_LEVEL_INF);

int main(void)
{
    int ret = platform_init();
    if (ret != HAL_OK) {
        printk("platform_init failed: %d\n", ret);
        return ret;
    }

    ret = hal_audio_init();
    if (ret != HAL_OK) {
        printk("hal_audio_init failed: %d\n", ret);
        return ret;
    }

    app_rtc_init();
    app_rtc_start();

    return 0;
}

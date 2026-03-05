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
    (void)app_db_init();
    ret = app_bus_start();
    if (ret != HAL_OK) {
        printk("app_bus_start failed: %d\n", ret);
        return ret;
    }

#if IS_ENABLED(CONFIG_BOOT_TONE)
    boot_tone_play();
#endif

    ret = app_lifecycle_start_all();
    if (ret != HAL_OK) {
        printk("app lifecycle start_all failed: %d\n", ret);
        return ret;
    }

    app_lifecycle_status_t ppg_st = {0};
    if (app_lifecycle_get_status(APP_LC_PPG, &ppg_st) == HAL_OK && ppg_st.started) {
        printk("gh3026 run start\n");
    }

    return 0;
}

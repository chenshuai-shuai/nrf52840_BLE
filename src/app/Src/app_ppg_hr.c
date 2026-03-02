#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>

#include "app_ppg_hr.h"
#include "hal_ppg.h"
#include "error.h"
#include "rt_thread.h"

LOG_MODULE_REGISTER(app_ppg_hr, LOG_LEVEL_INF);

#define APP_PPG_HR_STACK_SIZE 3072
#define APP_PPG_HR_PRIORITY   7

static struct k_thread g_ppg_hr_thread;
RT_THREAD_STACK_DEFINE(g_ppg_hr_stack, APP_PPG_HR_STACK_SIZE);

static void app_ppg_hr_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    uint32_t timeout_cnt = 0;
    int64_t last_timeout_log_ms = k_uptime_get();

    while (1) {
        hal_ppg_sample_t sample;
        int ret = hal_ppg_read(&sample, sizeof(sample), 2000);
        if (ret == HAL_OK) {
            printk("ppg hr: bpm=%d conf=%d snr=%d frame=%u ts=%u\n",
                   (int)sample.hr_bpm,
                   (int)sample.confidence,
                   (int)sample.snr,
                   (unsigned int)sample.frame_id,
                   (unsigned int)sample.timestamp_ms);
            timeout_cnt = 0;
            last_timeout_log_ms = k_uptime_get();
        } else if (ret == -ETIMEDOUT) {
            timeout_cnt++;
            int64_t now_ms = k_uptime_get();
            if ((now_ms - last_timeout_log_ms) >= 5000) {
                LOG_INF("ppg hr: waiting result, queue timeout cnt=%u",
                        (unsigned int)timeout_cnt);
                last_timeout_log_ms = now_ms;
            }
        } else {
            LOG_WRN("ppg hr: read error=%d", ret);
        }
    }
}

int app_ppg_hr_start(void)
{
    static bool started;

    if (started) {
        return HAL_OK;
    }

    int ret = hal_ppg_init();
    if (ret != HAL_OK) {
        LOG_ERR("gh3026 init failed: %d", ret);
        return ret;
    }

    ret = hal_ppg_start();
    if (ret != HAL_OK) {
        LOG_ERR("gh3026 start failed: %d", ret);
        return ret;
    }

    ret = rt_thread_start(&g_ppg_hr_thread,
                          g_ppg_hr_stack,
                          K_THREAD_STACK_SIZEOF(g_ppg_hr_stack),
                          app_ppg_hr_thread_entry,
                          NULL, NULL, NULL,
                          APP_PPG_HR_PRIORITY, 0,
                          "ppg_hr");
    if (ret != 0) {
        LOG_ERR("ppg_hr thread start failed: %d", ret);
        return HAL_EIO;
    }

    started = true;
    LOG_INF("gh3026 app start");
    return HAL_OK;
}

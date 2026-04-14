#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <limits.h>

#include "hal_ppg.h"
#include "error.h"
#include "gh3x2x_demo.h"
#include "gh3x2x_drv.h"

extern void gh3x2x_port_dump_state(const char *tag);

#if IS_ENABLED(CONFIG_PPG_SPI_PROBE)
LOG_MODULE_REGISTER(ppg_nrf, LOG_LEVEL_INF);
#else
LOG_MODULE_REGISTER(ppg_nrf, LOG_LEVEL_WRN);
#endif

#define GH3026_THREAD_STACK_SIZE 4096
#define GH3026_THREAD_PRIORITY   6

static struct k_thread g_gh_thread;
K_THREAD_STACK_DEFINE(g_gh_stack, GH3026_THREAD_STACK_SIZE);
K_MSGQ_DEFINE(g_ppg_sample_q, sizeof(hal_ppg_sample_t), 8, 4);
K_SEM_DEFINE(g_gh_irq_sem, 0, UINT_MAX);

extern GU8 g_uchGh3x2xIntCallBackIsCalled;

static struct {
    bool inited;
    bool started;
    bool run;
    uint32_t irq_count;
} g_ppg;

static struct {
    int32_t spo2_hb;
    int32_t spo2;
    int32_t confidence;
    int32_t valid_level;
    int32_t invalid_flag;
    uint32_t frame_id;
    uint32_t timestamp_ms;
    bool valid;
} g_spo2_latest;

#define PPG_SPO2_STALE_MS 10000U

void ppg_nrf_on_irq(void)
{
    k_sem_give(&g_gh_irq_sem);
}

void ppg_nrf_on_hr_result(int32_t hr_bpm, int32_t confidence, int32_t snr, uint32_t frame_id)
{
    uint32_t now_ms = (uint32_t)k_uptime_get();
    hal_ppg_sample_t sample = {
        .hr_bpm = hr_bpm,
        .spo2_hb = 0,
        .spo2 = 0,
        .spo2_confidence = 0,
        .spo2_valid_level = 0,
        .spo2_invalid_flag = 1,
        .hrv = 0,
        .hrv_confidence = 0,
        .confidence = confidence,
        .snr = snr,
        .frame_id = frame_id,
        .timestamp_ms = now_ms,
    };

    if (g_spo2_latest.valid &&
        ((uint32_t)(now_ms - g_spo2_latest.timestamp_ms) <= PPG_SPO2_STALE_MS)) {
        sample.spo2_hb = g_spo2_latest.spo2_hb;
        sample.spo2 = g_spo2_latest.spo2;
        sample.spo2_confidence = g_spo2_latest.confidence;
        sample.spo2_valid_level = g_spo2_latest.valid_level;
        sample.spo2_invalid_flag = g_spo2_latest.invalid_flag;
    }

    int ret = k_msgq_put(&g_ppg_sample_q, &sample, K_NO_WAIT);
    if (ret == -ENOMSG || ret == -EAGAIN) {
        hal_ppg_sample_t drop;
        (void)k_msgq_get(&g_ppg_sample_q, &drop, K_NO_WAIT);
        (void)k_msgq_put(&g_ppg_sample_q, &sample, K_NO_WAIT);
    }
}

void ppg_nrf_on_hrv_result(int32_t hrv, int32_t hrv_confidence, uint32_t frame_id)
{
    ARG_UNUSED(hrv);
    ARG_UNUSED(hrv_confidence);
    ARG_UNUSED(frame_id);
}

void ppg_nrf_on_spo2_result(int32_t spo2,
                            int32_t spo2_hb,
                            int32_t spo2_confidence,
                            int32_t spo2_valid_level,
                            int32_t spo2_invalid_flag,
                            uint32_t frame_id)
{
    g_spo2_latest.spo2_hb = spo2_hb;
    g_spo2_latest.spo2 = spo2;
    g_spo2_latest.confidence = spo2_confidence;
    g_spo2_latest.valid_level = spo2_valid_level;
    g_spo2_latest.invalid_flag = spo2_invalid_flag;
    g_spo2_latest.frame_id = frame_id;
    g_spo2_latest.timestamp_ms = (uint32_t)k_uptime_get();
    g_spo2_latest.valid = true;
}

static void gh3026_irq_worker(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    int64_t last_log_ms = k_uptime_get();

    while (g_ppg.run) {
        if (k_sem_take(&g_gh_irq_sem, K_MSEC(1000)) == 0) {
            if (!g_ppg.run) {
                break;
            }
            Gh3x2xDemoInterruptProcess();
            g_ppg.irq_count++;
        }

        int64_t now_ms = k_uptime_get();
        if (!IS_ENABLED(CONFIG_PPG_TUNE_MODE) && (now_ms - last_log_ms) >= 3000) {
            LOG_INF("gh3026 run: irq_count=%u int_flag=%u",
                    (unsigned int)g_ppg.irq_count,
                    (unsigned int)g_uchGh3x2xIntCallBackIsCalled);
            last_log_ms = now_ms;
        }
    }

    /* Signal that thread has exited cleanly */
    g_ppg.started = false;
    LOG_INF("gh3026 irq worker exited");
}

static int ppg_nrf_init(void)
{
    if (g_ppg.inited) {
        return HAL_OK;
    }

    gh3x2x_port_dump_state("ppg_pre_init");
    int ret = Gh3x2xDemoInit();
    if (ret != GH3X2X_RET_OK) {
        gh3x2x_port_dump_state("ppg_init_fail");
        LOG_ERR("gh3026 init failed: %d", ret);
        return HAL_EIO;
    }

    g_ppg.inited = true;
    if (!IS_ENABLED(CONFIG_PPG_TUNE_MODE)) {
        LOG_INF("gh3026 demo init ok");
    }
    return HAL_OK;
}

static int ppg_nrf_start(void)
{
    if (!g_ppg.inited) {
        return HAL_ENODEV;
    }
    if (g_ppg.started) {
        return HAL_OK;
    }

    g_uchGh3x2xIntCallBackIsCalled = 0;
    g_ppg.irq_count = 0;
    g_spo2_latest.valid = false;
    k_sem_reset(&g_gh_irq_sem);
    k_msgq_purge(&g_ppg_sample_q);
    Gh3x2xDemoStartSampling(GH3X2X_FUNCTION_HR | GH3X2X_FUNCTION_SPO2);
    GU16 wm_before = GH3X2X_GetCurrentFifoWaterLine();
    GS8 wm_ret = GH3X2X_FifoWatermarkThrConfig(16);
    GU16 wm_after = GH3X2X_GetCurrentFifoWaterLine();
    if (!IS_ENABLED(CONFIG_PPG_TUNE_MODE)) {
        LOG_INF("gh3026 wm: before=%u set_ret=%d after=%u",
                (unsigned int)wm_before,
                (int)wm_ret,
                (unsigned int)wm_after);
        for (GU8 slot = 0; slot <= 5; slot++) {
            GU8 drv0 = GH3X2X_GetSlotLedCurrent(slot, 0);
            GU8 drv1 = GH3X2X_GetSlotLedCurrent(slot, 1);
            LOG_INF("gh3026 ledcfg: slot=%u drv0=%u drv1=%u",
                    (unsigned int)slot,
                    (unsigned int)drv0,
                    (unsigned int)drv1);
        }
    }

    g_ppg.run = true;
    (void)k_thread_create(&g_gh_thread,
                          g_gh_stack,
                          K_THREAD_STACK_SIZEOF(g_gh_stack),
                          gh3026_irq_worker,
                          NULL, NULL, NULL,
                          GH3026_THREAD_PRIORITY,
                          0,
                          K_NO_WAIT);
    k_thread_name_set(&g_gh_thread, "gh3026_irq");

    g_ppg.started = true;
    if (!IS_ENABLED(CONFIG_PPG_TUNE_MODE)) {
        LOG_INF("gh3026 sampling start (HR+SPO2)");
    }
    return HAL_OK;
}

static int ppg_nrf_stop(void)
{
    if (!g_ppg.started) {
        return HAL_OK;
    }

    /* Cooperative shutdown: signal thread to exit, then wait */
    g_ppg.run = false;
    k_sem_give(&g_gh_irq_sem); /* Wake thread so it checks run flag */

    /* Wait for thread to exit naturally (up to 1s) */
    for (int i = 0; i < 50; i++) {
        if (!g_ppg.started) {
            break;
        }
        k_msleep(20);
    }

    /* If thread didn't exit in time, force abort as last resort */
    if (g_ppg.started) {
        LOG_WRN("gh3026 irq worker did not exit in time, forcing abort");
        k_thread_abort(&g_gh_thread);
        g_ppg.started = false;
    }

    k_msgq_purge(&g_ppg_sample_q);

    Gh3x2xDemoStopSampling(GH3X2X_FUNCTION_HR | GH3X2X_FUNCTION_SPO2);
    g_spo2_latest.valid = false;
    if (!IS_ENABLED(CONFIG_PPG_TUNE_MODE)) {
        LOG_INF("gh3026 sampling stop");
    }
    return HAL_OK;
}

static int ppg_nrf_read(void *buf, size_t len, int timeout_ms)
{
    if (buf == NULL || len < sizeof(hal_ppg_sample_t)) {
        return HAL_EINVAL;
    }
    if (!g_ppg.started) {
        return HAL_ENODEV;
    }

    k_timeout_t timeout = K_NO_WAIT;
    if (timeout_ms < 0) {
        timeout = K_FOREVER;
    } else if (timeout_ms > 0) {
        timeout = K_MSEC(timeout_ms);
    }

    int ret = k_msgq_get(&g_ppg_sample_q, buf, timeout);
    if (ret == 0) {
        return HAL_OK;
    }
    if (ret == -EAGAIN) {
        return -ETIMEDOUT;
    }
    return HAL_EIO;
}

static const hal_ppg_ops_t g_ppg_ops = {
    .init = ppg_nrf_init,
    .start = ppg_nrf_start,
    .stop = ppg_nrf_stop,
    .read = ppg_nrf_read,
};

int ppg_nrf_register(void)
{
    return hal_ppg_register(&g_ppg_ops);
}

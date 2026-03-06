#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "app_spk_diag.h"
#include "hal_spk.h"

LOG_MODULE_REGISTER(app_spk_diag, LOG_LEVEL_INF);

#define SPK_DIAG_STACK_SIZE 2048
#define SPK_DIAG_PRIORITY   9
#define SPK_DIAG_FRAME_SAMPLES 160  /* 10ms @ 16k */
#define SPK_DIAG_SAMPLE_RATE   16000
#define SPK_DIAG_FRAME_MS      10

static struct k_thread g_spk_diag_thread;
K_THREAD_STACK_DEFINE(g_spk_diag_stack, SPK_DIAG_STACK_SIZE);
static bool g_started;

static void gen_square(int16_t *out, size_t samples, size_t half_period, int16_t amp)
{
    for (size_t i = 0; i < samples; i++) {
        size_t slot = (half_period == 0) ? 0 : ((i / half_period) & 1U);
        out[i] = slot ? amp : (int16_t)-amp;
    }
}

static void app_spk_diag_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    int ret = hal_spk_init();
    if (ret != 0) {
        LOG_ERR("spk diag: hal_spk_init failed: %d", ret);
        return;
    }
    LOG_INF("spk diag: hal_spk_init ok");

    int16_t tone_hi[SPK_DIAG_FRAME_SAMPLES];
    int16_t tone_lo[SPK_DIAG_FRAME_SAMPLES];
    int16_t silence[SPK_DIAG_FRAME_SAMPLES];
    memset(silence, 0, sizeof(silence));
    /* ~1.8kHz and ~1.1kHz square tones for easy audibility */
    gen_square(tone_hi, SPK_DIAG_FRAME_SAMPLES, SPK_DIAG_SAMPLE_RATE / (1800U * 2U), 7000);
    gen_square(tone_lo, SPK_DIAG_FRAME_SAMPLES, SPK_DIAG_SAMPLE_RATE / (1100U * 2U), 6000);

    for (;;) {
        ret = hal_spk_start();
        if (ret != 0) {
            LOG_ERR("spk diag: hal_spk_start failed: %d", ret);
            k_msleep(1000);
            continue;
        }
        LOG_INF("spk diag: hal_spk_start ok");

        uint32_t frame_cnt = 0;
        int64_t next_deadline = k_uptime_get();
        for (;;) {
            const int16_t *frm = ((frame_cnt / 50U) & 1U) ? tone_hi : tone_lo;
            int rc = hal_spk_write(frm, sizeof(tone_hi), 1000);
            if (rc != 0) {
                LOG_ERR("spk diag: hal_spk_write(continuous) failed: %d frame=%u", rc, (unsigned)frame_cnt);
                (void)hal_spk_stop();
                break;
            }
            frame_cnt++;

            if ((frame_cnt % 200U) == 0U) {
                LOG_INF("spk diag: continuous tx frames=%u", (unsigned)frame_cnt);
            }

            /* Pace producer to real-time (10 ms audio per frame). */
            next_deadline += SPK_DIAG_FRAME_MS;
            int64_t now = k_uptime_get();
            if (next_deadline > now) {
                k_msleep((int32_t)(next_deadline - now));
            } else {
                /* Producer lagged; re-sync to avoid drift accumulation. */
                next_deadline = now;
            }
        }

        k_msleep(1000);
    }
}

int app_spk_diag_start(void)
{
    if (g_started) {
        return 0;
    }

    k_thread_create(&g_spk_diag_thread,
                    g_spk_diag_stack,
                    K_THREAD_STACK_SIZEOF(g_spk_diag_stack),
                    app_spk_diag_entry,
                    NULL, NULL, NULL,
                    SPK_DIAG_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&g_spk_diag_thread, "spk_diag");
    g_started = true;
    LOG_INF("spk diag: started");
    return 0;
}

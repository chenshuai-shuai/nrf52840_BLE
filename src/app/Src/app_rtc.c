#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/byteorder.h>
#include <stdint.h>
#include <string.h>

#include "app_rtc.h"
#include "app_state.h"
#include "hal_audio.h"
#include "hal_spk.h"
#include "hal_mic.h"
#include "error.h"

#include <zephyr/logging/log.h>
#include "rt_thread.h"
#include "app_uplink_service.h"

LOG_MODULE_REGISTER(app_rtc, LOG_LEVEL_INF);

#ifndef AUDIO_PKT_MAGIC0
#define AUDIO_PKT_MAGIC0 0xA5
#define AUDIO_PKT_MAGIC1 0x5A
#define AUDIO_PAYLOAD_MAX 200
#endif

#ifndef DOWNLINK_TEST
#define DOWNLINK_TEST 0
#endif

struct audio_pkt_hdr {
    uint8_t magic0;
    uint8_t magic1;
    uint16_t seq;
    uint8_t frag_idx;
    uint8_t frag_cnt;
    uint16_t payload_len;
} __packed;

#ifdef CONFIG_SPK_STREAM
#include "hal_ble.h"

#define SPK_STREAM_STACK_SIZE 4096
#define SPK_STREAM_PRIO 4
#define SPK_PLAY_STACK_SIZE 2048
#define SPK_PLAY_PRIO 5

#define SPK_FRAME_SAMPLES 320U /* 20 ms @ 16 kHz */
#define SPK_FRAME_BYTES (SPK_FRAME_SAMPLES * sizeof(int16_t))
#define SPK_FRAME_Q_LEN 24
#define SPK_BUF_LOW_WATER 2
#define SPK_PREBUFFER_FRAMES 6
#define SPK_TEST_BUF_MAX (96 * 1024)

struct spk_frame_msg {
    uint8_t *buf;
    size_t len;
    bool end;
};

K_MEM_SLAB_DEFINE(spk_rx_slab, SPK_FRAME_BYTES, SPK_FRAME_Q_LEN, 4);
K_MSGQ_DEFINE(g_spk_frame_q, sizeof(struct spk_frame_msg), SPK_FRAME_Q_LEN, 4);

static struct k_thread g_spk_rx_thread;
RT_THREAD_STACK_DEFINE(g_spk_rx_stack, SPK_STREAM_STACK_SIZE);

static struct k_thread g_spk_play_thread;
RT_THREAD_STACK_DEFINE(g_spk_play_stack, SPK_PLAY_STACK_SIZE);
static volatile bool g_spk_buf_full = false;
static volatile bool g_spk_accept_audio = false;
static volatile bool g_nrf_ready_sent = false;
static uint32_t g_spk_rx_frames = 0;
static uint32_t g_spk_play_frames = 0;
static uint32_t g_spk_play_errs = 0;

static void mic_drop_queue(void);

static uint8_t g_spk_test_buf[SPK_TEST_BUF_MAX];
static size_t g_spk_test_len = 0;
static struct k_sem g_spk_test_sem;
static uint32_t g_spk_test_overflow = 0;

static bool handle_ctrl_msg(const uint8_t *buf, int len)
{
    if (len <= 0 || buf == NULL) {
        return false;
    }
    if (len >= 14 && memcmp(buf, "APP_PLAY_START", 14) == 0) {
        LOG_INF("CTRL APP_PLAY_START");
        g_spk_accept_audio = true;
        app_state_set(AUDIO_MODE_PLAY);
        g_nrf_ready_sent = false;
        g_spk_test_len = 0;
        g_spk_test_overflow = 0;
        mic_drop_queue();
        return true;
    }
    if (len >= 10 && memcmp(buf, "APP_READY?", 10) == 0) {
        LOG_INF("CTRL APP_READY?");
        g_spk_accept_audio = false;
        g_nrf_ready_sent = false;
        mic_drop_queue();
        if (hal_ble_is_ready()) {
            const char ready[] = "NRF_READY";
            (void)app_uplink_publish(APP_DATA_PART_CTRL,
                                     APP_UPLINK_PRIO_HIGH,
                                     ready,
                                     sizeof(ready) - 1,
                                     (uint32_t)k_uptime_get());
            g_nrf_ready_sent = true;
            LOG_INF("CTRL NRF_READY sent");
        }
        return true;
    }
    if (len >= 12 && memcmp(buf, "APP_PLAY_END", 12) == 0) {
        LOG_INF("CTRL APP_PLAY_END");
        g_spk_accept_audio = false;
        app_state_set(AUDIO_MODE_UPLOAD);
        return true;
    }
    return false;
}

static uint8_t g_spk_stream_buf[SPK_FRAME_BYTES * 8];
static uint8_t g_spk_pkt_buf[244];

static void spk_rx_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    size_t stream_len = 0;
    int64_t last_log = 0;
    int64_t last_audio_ms = 0;

    while (1) {
        int r = hal_ble_recv(g_spk_pkt_buf, sizeof(g_spk_pkt_buf), 1000);
        if (r <= 0) {
            if (g_spk_accept_audio && g_spk_test_len > 0) {
                int64_t now = k_uptime_get();
                if (last_audio_ms != 0 && (now - last_audio_ms) > 600) {
                    LOG_INF("SPK RX idle end, bytes=%u overflow=%u",
                            (unsigned)g_spk_test_len,
                            (unsigned)g_spk_test_overflow);
                    last_audio_ms = 0;
                    k_sem_give(&g_spk_test_sem);
                }
            }
            continue;
        }
        if (handle_ctrl_msg(g_spk_pkt_buf, r)) {
            continue;
        }
        if (r < (int)sizeof(struct audio_pkt_hdr)) {
            continue;
        }

        struct audio_pkt_hdr *hdr = (struct audio_pkt_hdr *)g_spk_pkt_buf;
        if (hdr->magic0 != AUDIO_PKT_MAGIC0 || hdr->magic1 != AUDIO_PKT_MAGIC1) {
            continue;
        }
        if (!g_spk_accept_audio) {
            continue;
        }

        uint16_t payload_len = sys_get_le16((uint8_t *)&hdr->payload_len);
        uint16_t avail = (uint16_t)(r - sizeof(struct audio_pkt_hdr));
        if (payload_len > avail) {
            payload_len = avail;
        }

        if (hdr->frag_cnt == 0 && payload_len == 0) {
            LOG_INF("SPK RX end, bytes=%u overflow=%u",
                    (unsigned)g_spk_test_len,
                    (unsigned)g_spk_test_overflow);
            stream_len = 0;
            last_audio_ms = 0;
            k_sem_give(&g_spk_test_sem);
            continue;
        }
        if (payload_len == 0) {
            continue;
        }

        if (payload_len > sizeof(g_spk_stream_buf)) {
            stream_len = 0;
            continue;
        }
        if (stream_len + payload_len > sizeof(g_spk_stream_buf)) {
            stream_len = 0;
        }
        size_t space = sizeof(g_spk_test_buf) - g_spk_test_len;
        if (space > 0) {
            size_t take = (payload_len <= space) ? payload_len : space;
            memcpy(g_spk_test_buf + g_spk_test_len,
                   g_spk_pkt_buf + sizeof(struct audio_pkt_hdr),
                   take);
            g_spk_test_len += take;
            if (take < payload_len) {
                g_spk_test_overflow++;
            }
        } else {
            g_spk_test_overflow++;
        }
        int64_t now = k_uptime_get();
        last_audio_ms = now;
        if (now - last_log >= 1000) {
            last_log = now;
            LOG_INF("SPK RX bytes=%u overflow=%u",
                    (unsigned)g_spk_test_len,
                    (unsigned)g_spk_test_overflow);
        }
    }
}

static void spk_play_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    int ret = hal_spk_init();
    if (ret != HAL_OK) {
        LOG_ERR("spk init failed: %d", ret);
        return;
    }

    bool eos_pending = false;
    int64_t last_log = 0;
    bool spk_running = false;
    uint8_t *pending_buf = NULL;
    size_t pending_len = 0;
    static int16_t silence_frame[SPK_FRAME_SAMPLES] = {0};
    static int16_t stereo_frame[SPK_FRAME_SAMPLES * 2];
    /* Lightweight DSP state (per-channel mono) */
    static float dc_x1 = 0.0f;
    static float dc_y1 = 0.0f;
    static float lp_y1 = 0.0f;
    /* Simple voice EQ (two peaking biquads) */
    static float eq1_x1 = 0.0f, eq1_x2 = 0.0f, eq1_y1 = 0.0f, eq1_y2 = 0.0f;
    static float eq2_x1 = 0.0f, eq2_x2 = 0.0f, eq2_y1 = 0.0f, eq2_y2 = 0.0f;

    while (1) {
        k_sem_take(&g_spk_test_sem, K_FOREVER);
        if (g_spk_test_len == 0) {
            continue;
        }
        ret = hal_spk_start();
        if (ret != HAL_OK) {
            LOG_ERR("spk start failed: %d", ret);
            continue;
        }
        spk_running = true;
        size_t off = 0;
        while (off < g_spk_test_len) {
            size_t left = g_spk_test_len - off;
            size_t chunk = left > SPK_FRAME_BYTES ? SPK_FRAME_BYTES : left;
            const int16_t *mono = (const int16_t *)(g_spk_test_buf + off);
            size_t mono_samples = chunk / sizeof(int16_t);
            if (mono_samples > SPK_FRAME_SAMPLES) {
                mono_samples = SPK_FRAME_SAMPLES;
            }
            for (size_t i = 0; i < SPK_FRAME_SAMPLES; i++) {
                float x = (i < mono_samples) ? (float)mono[i] : 0.0f;
                /* DC blocker / high-pass: y = x - x1 + R*y1 */
                const float dc_r = 0.99f;
                float y = x - dc_x1 + dc_r * dc_y1;
                dc_x1 = x;
                dc_y1 = y;
                /* Pre-gain attenuation to reduce noise floor */
                const float pre_gain = 1.10f;
                y *= pre_gain;
                /* Voice EQ: +3dB @ 1.5k (Q=1.0), +2dB @ 4.5k (Q=0.9) */
                {
                    const float b0 = 1.0781544f, b1 = -1.3478988f, b2 = 0.54294974f;
                    const float a1 = -1.3478988f, a2 = 0.6211041f;
                    float out = b0 * y + b1 * eq1_x1 + b2 * eq1_x2 - a1 * eq1_y1 - a2 * eq1_y2;
                    eq1_x2 = eq1_x1;
                    eq1_x1 = y;
                    eq1_y2 = eq1_y1;
                    eq1_y1 = out;
                    y = out;
                }
                {
                    const float b0 = 1.0846382f, b1 = 0.2626373f, b2 = 0.26159608f;
                    const float a1 = 0.2626373f, a2 = 0.34623435f;
                    float out = b0 * y + b1 * eq2_x1 + b2 * eq2_x2 - a1 * eq2_y1 - a2 * eq2_y2;
                    eq2_x2 = eq2_x1;
                    eq2_x1 = y;
                    eq2_y2 = eq2_y1;
                    eq2_y1 = out;
                    y = out;
                }
                /* Gentle low-pass to reduce HF hiss (approx 7 kHz @ 16 kHz) */
                const float lp_a = 0.25f;
                lp_y1 = lp_a * lp_y1 + (1.0f - lp_a) * y;
                y = lp_y1;
                /* Noise gate with soft knee to reduce hiss on silence */
                const float gate_th = 250.0f;
                const float gate_knee = 150.0f;
                float ay = y >= 0.0f ? y : -y;
                if (ay < gate_th) {
                    float t = ay / gate_th;
                    float gain = (t < (gate_knee / gate_th)) ? (t * t) : t;
                    y *= gain;
                }
                /* Soft limiter */
                const float limit = 30000.0f;
                if (y > limit) {
                    y = limit + (y - limit) * 0.50f;
                } else if (y < -limit) {
                    y = -limit + (y + limit) * 0.50f;
                }
                if (y > 32767.0f) {
                    y = 32767.0f;
                } else if (y < -32768.0f) {
                    y = -32768.0f;
                }
                int16_t v = (int16_t)y;
                stereo_frame[i * 2] = v;
                stereo_frame[i * 2 + 1] = v;
            }

            uint8_t *st = (uint8_t *)stereo_frame;
            size_t stereo_bytes = SPK_FRAME_SAMPLES * 2 * sizeof(int16_t);
            size_t wrote = 0;
            while (wrote < stereo_bytes) {
                size_t blk = SPK_FRAME_BYTES;
                int w = hal_spk_write(st + wrote, blk, 1000);
                if (w != HAL_OK) {
                    g_spk_play_errs++;
                    LOG_ERR("spk write err: %d", w);
                    wrote = stereo_bytes;
                    break;
                }
                g_spk_play_frames++;
                wrote += blk;
            }
            off += chunk;
        }
        g_spk_test_len = 0;
        if (spk_running) {
            (void)hal_spk_stop();
            spk_running = false;
        }
        const char done[] = "PLAY_DONE";
        if (hal_ble_is_ready()) {
            (void)app_uplink_publish(APP_DATA_PART_CTRL,
                                     APP_UPLINK_PRIO_HIGH,
                                     done,
                                     sizeof(done) - 1,
                                     (uint32_t)k_uptime_get());
        }
        if (!DOWNLINK_TEST) {
            app_state_set(AUDIO_MODE_UPLOAD);
        }
        g_spk_accept_audio = false;
    }

    while (1) {
        struct spk_frame_msg msg;
        int q = k_msgq_get(&g_spk_frame_q, &msg, K_MSEC(1000));
        if (q == 0) {
            if (msg.end) {
                eos_pending = true;
                continue;
            }
            if (msg.buf && msg.len > 0) {
                if (!spk_running) {
                    /* Buffer one frame before starting to reduce underrun risk. */
                    if (pending_buf == NULL) {
                        pending_buf = msg.buf;
                        pending_len = msg.len;
                        continue;
                    }
                    if (k_msgq_num_used_get(&g_spk_frame_q) < SPK_PREBUFFER_FRAMES) {
                        /* Keep buffering until we have enough prebuffer. */
                        k_mem_slab_free(&spk_rx_slab, msg.buf);
                        continue;
                    }
                    ret = hal_spk_start();
                    if (ret != HAL_OK) {
                        LOG_ERR("spk start failed: %d", ret);
                        if (pending_buf != NULL) {
                            k_mem_slab_free(&spk_rx_slab, pending_buf);
                            pending_buf = NULL;
                            pending_len = 0;
                        }
                        k_mem_slab_free(&spk_rx_slab, msg.buf);
                        continue;
                    }
                    spk_running = true;
                    if (pending_buf != NULL) {
                        int wp = hal_spk_write(pending_buf, pending_len, 1000);
                        k_mem_slab_free(&spk_rx_slab, pending_buf);
                        pending_buf = NULL;
                        pending_len = 0;
                        if (wp != HAL_OK) {
                            g_spk_play_errs++;
                            LOG_ERR("spk write err: %d", wp);
                            (void)hal_spk_stop();
                            spk_running = false;
                            k_mem_slab_free(&spk_rx_slab, msg.buf);
                            continue;
                        } else {
                            g_spk_play_frames++;
                        }
                    }
                }
                int w = hal_spk_write(msg.buf, msg.len, 1000);
                if (w != HAL_OK) {
                    g_spk_play_errs++;
                    LOG_ERR("spk write err: %d", w);
                    (void)hal_spk_stop();
                    spk_running = false;
                } else {
                    g_spk_play_frames++;
                }
                k_mem_slab_free(&spk_rx_slab, msg.buf);
            }
        }
        if (q != 0 && spk_running && !eos_pending) {
            /* Feed silence to keep I2S alive if BLE jitter causes gaps. */
            (void)hal_spk_write(silence_frame, sizeof(silence_frame), 1000);
        }

        int64_t now = k_uptime_get();
        if (now - last_log >= 1000) {
            last_log = now;
            LOG_INF("SPK PLAY frames=%u errs=%u q=%u",
                    g_spk_play_frames, g_spk_play_errs,
                    (unsigned)k_msgq_num_used_get(&g_spk_frame_q));
        }

        if (g_spk_buf_full) {
            size_t used = k_msgq_num_used_get(&g_spk_frame_q);
            if (used <= SPK_BUF_LOW_WATER && hal_ble_is_ready()) {
                const char low[] = "BUF_LOW";
                (void)app_uplink_publish(APP_DATA_PART_CTRL,
                                         APP_UPLINK_PRIO_HIGH,
                                         low,
                                         sizeof(low) - 1,
                                         (uint32_t)k_uptime_get());
                g_spk_buf_full = false;
            }
        }

        if (q != 0 && eos_pending && pending_buf != NULL && !spk_running) {
            ret = hal_spk_start();
            if (ret == HAL_OK) {
                spk_running = true;
                int wp = hal_spk_write(pending_buf, pending_len, 1000);
                k_mem_slab_free(&spk_rx_slab, pending_buf);
                pending_buf = NULL;
                pending_len = 0;
                if (wp != HAL_OK) {
                    g_spk_play_errs++;
                    LOG_ERR("spk write err: %d", wp);
                    (void)hal_spk_stop();
                    spk_running = false;
                } else {
                    g_spk_play_frames++;
                }
            } else {
                LOG_ERR("spk start failed: %d", ret);
            }
        }

        if (eos_pending && k_msgq_num_used_get(&g_spk_frame_q) == 0 && pending_buf == NULL) {
            const char done[] = "PLAY_DONE";
            if (hal_ble_is_ready()) {
                (void)app_uplink_publish(APP_DATA_PART_CTRL,
                                         APP_UPLINK_PRIO_HIGH,
                                         done,
                                         sizeof(done) - 1,
                                         (uint32_t)k_uptime_get());
            }
            eos_pending = false;
            g_spk_accept_audio = false;
            if (!DOWNLINK_TEST) {
                app_state_set(AUDIO_MODE_UPLOAD);
            }
            if (spk_running) {
                (void)hal_spk_stop();
                spk_running = false;
            }
        }
    }
}
#endif

#ifdef CONFIG_MIC_LEVEL_TEST
#define MIC_LEVEL_STACK_SIZE 2048
#define MIC_LEVEL_PRIO 4

static struct k_thread g_mic_level_thread;
RT_THREAD_STACK_DEFINE(g_mic_level_stack, MIC_LEVEL_STACK_SIZE);

static void mic_level_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

#if defined(CONFIG_MIC_TEST)
    LOG_ERR("MIC level test disabled: MIC_TEST is enabled");
    return;
#endif

    int ret = hal_mic_init();
    if (ret != HAL_OK) {
        LOG_ERR("mic level: hal_mic_init failed: %d", ret);
        return;
    }

    ret = hal_mic_start();
    if (ret != HAL_OK) {
        LOG_ERR("mic level: hal_mic_start failed: %d", ret);
        return;
    }

    LOG_INF("MIC level test started");

    uint64_t last_log = k_uptime_get();
    uint32_t frames = 0;
    int16_t peak = 0;
    uint64_t sum_sq = 0;
    uint32_t sample_cnt = 0;

    while (1) {
        void *buf = NULL;
        size_t len = 0;
        int r = hal_mic_read_block(&buf, &len, 1000);
        if (r == HAL_OK && buf != NULL && len > 0) {
            int16_t *pcm = (int16_t *)buf;
            size_t samples = len / sizeof(int16_t);
            for (size_t i = 0; i < samples; i++) {
                int32_t v = pcm[i];
                int32_t av = v < 0 ? -v : v;
                if (av > peak) {
                    peak = (int16_t)av;
                }
                sum_sq += (uint64_t)(v * (int64_t)v);
            }
            sample_cnt += (uint32_t)samples;
            frames++;
            (void)hal_mic_release(buf);
        }

        uint64_t now = k_uptime_get();
        if (now - last_log >= 1000) {
            uint32_t rms = 0;
            if (sample_cnt > 0) {
                rms = (uint32_t)(sum_sq / sample_cnt);
            }
            LOG_INF("MIC level: frames=%u peak=%d rms=%u", frames, peak, rms);
            last_log = now;
            peak = 0;
            sum_sq = 0;
            sample_cnt = 0;
            frames = 0;
        }
    }
}
#endif

#ifdef CONFIG_SPK_BOOT_TONE
static void spk_boot_tone_play(void)
{
    int ret = hal_spk_init();
    if (ret != HAL_OK) {
        LOG_ERR("boot tone: hal_spk_init failed: %d", ret);
        return;
    }

    ret = hal_spk_start();
    if (ret != HAL_OK) {
        LOG_ERR("boot tone: hal_spk_start failed: %d", ret);
        return;
    }

    static const int16_t sine32[32] = {
        0, 2359, 4630, 6722, 8551, 10050, 11172, 11879,
        12140, 11939, 11276, 10167, 8650, 6774, 4601, 2362,
        0, -2362, -4601, -6774, -8650, -10167, -11276, -11939,
        -12140, -11879, -11172, -10050, -8551, -6722, -4630, -2359
    };

    int16_t buf[160];
    const uint32_t rate = 16000U;
    const uint32_t tone_hz[] = { 880U, 1175U, 1568U };
    const uint8_t tone_blocks[] = { 35, 35, 45 }; /* 10 ms per block */

    for (size_t t = 0; t < ARRAY_SIZE(tone_hz); t++) {
        uint32_t phase = 0;
        uint32_t step = (tone_hz[t] * 32U << 16) / rate;

        /* Tone duration in 10 ms blocks */
        for (int block = 0; block < tone_blocks[t]; block++) {
            for (int i = 0; i < 160; i++) {
                uint32_t idx = (phase >> 16) & 0x1FU;
                buf[i] = sine32[idx];
                phase += step;
            }
            ret = hal_spk_write(buf, sizeof(buf), 1000);
            if (ret != HAL_OK) {
                LOG_ERR("boot tone: hal_spk_write failed: %d", ret);
                (void)hal_spk_stop();
                return;
            }
        }

        /* 70 ms gap */
        memset(buf, 0, sizeof(buf));
        for (int block = 0; block < 7; block++) {
            ret = hal_spk_write(buf, sizeof(buf), 1000);
            if (ret != HAL_OK) {
                LOG_ERR("boot tone: silent write failed: %d", ret);
                (void)hal_spk_stop();
                return;
            }
        }
    }

    (void)hal_spk_stop();
}
#endif

#ifdef CONFIG_BLE_TEST
#include "hal_ble.h"

#define BLE_TEST_STACK_SIZE 2048
#define BLE_TEST_PRIO 3

static struct k_thread g_ble_test_thread;
RT_THREAD_STACK_DEFINE(g_ble_test_stack, BLE_TEST_STACK_SIZE);

static void ble_test_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    int ret = hal_ble_init();
    if (ret != HAL_OK) {
        LOG_ERR("hal_ble_init failed: %d", ret);
        return;
    }

    ret = hal_ble_start();
    if (ret != HAL_OK) {
        LOG_ERR("hal_ble_start failed: %d", ret);
        return;
    }

    LOG_INF("BLE test thread started");

    while (1) {
        uint8_t buf[244];
        int r = hal_ble_recv(buf, sizeof(buf), 1000);
        if (r > 0) {
            LOG_INF("BLE RX %d bytes", r);
        }
    }
}
#endif

#ifdef CONFIG_MIC_TEST
#include "hal_mic.h"
#include "hal_ble.h"
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>

#define MIC_TEST_STACK_SIZE 2048
#define MIC_TEST_PRIO 4
#define MIC_UPLOAD_STACK_SIZE 2048
#define MIC_UPLOAD_PRIO 5

#define MIC_FRAME_Q_LEN 16
static struct k_thread g_mic_test_thread;
RT_THREAD_STACK_DEFINE(g_mic_test_stack, MIC_TEST_STACK_SIZE);

static struct k_thread g_mic_upload_thread;
RT_THREAD_STACK_DEFINE(g_mic_upload_stack, MIC_UPLOAD_STACK_SIZE);

struct mic_frame_msg {
    void *buf;
    size_t len;
    uint16_t seq;
};

K_MSGQ_DEFINE(g_mic_frame_q, sizeof(struct mic_frame_msg), MIC_FRAME_Q_LEN, 4);

static int audio_send_frame(const uint8_t *pcm, size_t len, uint16_t seq)
{
    if (pcm == NULL || len == 0) {
        return HAL_EINVAL;
    }

    int mtu = hal_ble_get_mtu();
    if (mtu < 23) {
        mtu = 23;
    }
    size_t payload_max = AUDIO_PAYLOAD_MAX;
    if (mtu > 3) {
        size_t att_payload = (size_t)(mtu - 3);
        if (att_payload > sizeof(struct audio_pkt_hdr)) {
            size_t dyn_max = att_payload - sizeof(struct audio_pkt_hdr);
            if (dyn_max < payload_max) {
                payload_max = dyn_max;
            }
        } else {
            return HAL_EINVAL;
        }
    }

    uint16_t frame_seq = seq;
    uint8_t frag_cnt = (uint8_t)((len + payload_max - 1) / payload_max);
    size_t offset = 0;

    for (uint8_t frag = 0; frag < frag_cnt; frag++) {
        size_t remaining = len - offset;
        size_t chunk = (remaining > payload_max) ? payload_max : remaining;

        uint8_t pkt[sizeof(struct audio_pkt_hdr) + AUDIO_PAYLOAD_MAX];
        struct audio_pkt_hdr *hdr = (struct audio_pkt_hdr *)pkt;
        hdr->magic0 = AUDIO_PKT_MAGIC0;
        hdr->magic1 = AUDIO_PKT_MAGIC1;
        sys_put_le16(frame_seq, (uint8_t *)&hdr->seq);
        hdr->frag_idx = frag;
        hdr->frag_cnt = frag_cnt;
        sys_put_le16((uint16_t)chunk, (uint8_t *)&hdr->payload_len);

        memcpy(pkt + sizeof(struct audio_pkt_hdr), pcm + offset, chunk);

        int ret = hal_ble_send(pkt, sizeof(struct audio_pkt_hdr) + chunk, 0);
        if (ret != HAL_OK) {
            return ret;
        }

        offset += chunk;
        if (frag_cnt > 1) {
            k_usleep(200);
        }
    }

    return HAL_OK;
}

static void mic_capture_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

#ifdef CONFIG_SPK_BOOT_TONE
    /* Wait for boot tone to finish and let clocks settle. */
    while (app_state_get() == AUDIO_MODE_BOOT) {
        k_msleep(10);
    }
    k_msleep(600);
#endif

    printk("mic_capture: entry\n");
    LOG_INF("mic_capture: entry");

    while (1) {
        LOG_INF("mic_capture: init");
        int ret = hal_mic_init();
        if (ret != HAL_OK) {
            LOG_ERR("hal_mic_init failed: %d", ret);
            k_msleep(200);
            continue;
        }

        for (int i = 0; i < 5; i++) {
            LOG_INF("mic_capture: start attempt %d", i + 1);
            ret = hal_mic_start();
            if (ret == HAL_OK) {
                break;
            }
            LOG_ERR("hal_mic_start failed: %d (retry %d)", ret, i + 1);
            k_msleep(100);
        }
        if (ret != HAL_OK) {
            LOG_ERR("hal_mic_start failed: %d", ret);
            k_msleep(500);
            continue;
        }

        LOG_INF("MIC test thread started");

        while (1) {
            void *buf = NULL;
            size_t len = 0;
            int r = hal_mic_read_block(&buf, &len, 1000);
            if (r == HAL_OK && buf != NULL && len > 0) {
                if (!app_state_mic_enabled()) {
                    (void)hal_mic_release(buf);
                    continue;
                }
                static uint16_t seq = 0;
                struct mic_frame_msg msg = {
                    .buf = buf,
                    .len = len,
                    .seq = seq++,
                };
                int q = k_msgq_put(&g_mic_frame_q, &msg, K_NO_WAIT);
                if (q != 0) {
                    (void)hal_mic_release(buf);
                }
            } else if (r != HAL_OK) {
                LOG_ERR("hal_mic_read_block failed: %d", r);
                break;
            }
        }
        (void)hal_mic_stop();
        k_msleep(200);
    }
}

static void mic_upload_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    uint64_t last_log = k_uptime_get();
    uint32_t frames = 0;
    uint32_t frames_sent = 0;
    uint32_t frames_drop = 0;
    uint32_t pkts_sent = 0;
    uint32_t bytes_sent = 0;

    while (1) {
        struct mic_frame_msg msg;
        int q = k_msgq_get(&g_mic_frame_q, &msg, K_MSEC(1000));
        if (q != 0) {
            continue;
        }

        frames++;

        if (!app_state_mic_enabled()) {
            frames_drop++;
            (void)hal_mic_release(msg.buf);
            goto log_stats;
        }

        /* Real-time path: do not backlog audio before BLE notify channel is ready. */
        if (!hal_ble_is_ready() || hal_ble_get_mtu() < 23) {
            frames_drop++;
            (void)hal_mic_release(msg.buf);
            goto log_stats;
        }

        int ret = audio_send_frame((const uint8_t *)msg.buf, msg.len, msg.seq);
        if (ret == HAL_OK) {
            frames_sent++;
            pkts_sent += (uint32_t)((msg.len + AUDIO_PAYLOAD_MAX - 1) / AUDIO_PAYLOAD_MAX);
            bytes_sent += (uint32_t)msg.len;
        } else {
            frames_drop++;
        }

        (void)hal_mic_release(msg.buf);

log_stats:
        {
            uint64_t now = k_uptime_get();
            if (now - last_log >= 1000) {
                LOG_INF("MIC stream: frames=%u sent=%u drop=%u pkts=%u bytes=%u ready=%d mtu=%d",
                        frames, frames_sent, frames_drop, pkts_sent, bytes_sent,
                        hal_ble_is_ready(), hal_ble_get_mtu());
                last_log = now;
            }
        }
    }
}
#endif

static void mic_drop_queue(void)
{
#ifdef CONFIG_MIC_TEST
    struct mic_frame_msg msg;
    while (k_msgq_get(&g_mic_frame_q, &msg, K_NO_WAIT) == 0) {
        (void)hal_mic_release(msg.buf);
    }
#endif
}

static int ble_ensure_started(void)
{
    /* BLE init/start is centralized in app_uplink_service. */
    return HAL_OK;
}

void app_rtc_init(void)
{
    app_state_init(AUDIO_MODE_BOOT);

    LOG_INF("Build: DOWNLINK_TEST=%d MIC_TEST=%d SPK_STREAM=%d",
            DOWNLINK_TEST,
            IS_ENABLED(CONFIG_MIC_TEST),
            IS_ENABLED(CONFIG_SPK_STREAM));

    if (DOWNLINK_TEST) {
        LOG_INF("Downlink test mode enabled");
    }
}

void app_rtc_start(void)
{
    int ret = ble_ensure_started();
    if (ret != HAL_OK) {
        printk("ble_ensure_started failed: %d\n", ret);
        return;
    }

#ifdef CONFIG_MIC_LEVEL_TEST
    (void)rt_thread_start(&g_mic_level_thread,
                          g_mic_level_stack,
                          K_THREAD_STACK_SIZEOF(g_mic_level_stack),
                          mic_level_entry,
                          NULL, NULL, NULL,
                          MIC_LEVEL_PRIO,
                          0,
                          "mic_lvl");
#endif

#ifdef CONFIG_BLE_TEST
    (void)rt_thread_start(&g_ble_test_thread,
                          g_ble_test_stack,
                          K_THREAD_STACK_SIZEOF(g_ble_test_stack),
                          ble_test_entry,
                          NULL, NULL, NULL,
                          BLE_TEST_PRIO,
                          0,
                          "ble_test");
#endif

#ifdef CONFIG_SPK_BOOT_TONE
    /* Play boot tone first, then start mic upload. */
    app_state_set(AUDIO_MODE_BOOT);
    printk("boot_tone: start\n");
    LOG_INF("Boot tone start");
    spk_boot_tone_play();
    LOG_INF("Boot tone done");
    printk("boot_tone: done\n");
    if (!DOWNLINK_TEST) {
        app_state_set(AUDIO_MODE_UPLOAD);
    }
#else
    if (!DOWNLINK_TEST) {
        app_state_set(AUDIO_MODE_UPLOAD);
    }
#endif

#if defined(CONFIG_MIC_TEST) && (DOWNLINK_TEST == 0)
    printk("mic_threads: start\n");
    LOG_INF("Starting MIC threads");
    int mic_ret = rt_thread_start(&g_mic_test_thread,
                                  g_mic_test_stack,
                                  K_THREAD_STACK_SIZEOF(g_mic_test_stack),
                                  mic_capture_entry,
                                  NULL, NULL, NULL,
                                  MIC_TEST_PRIO,
                                  0,
                                  "mic_cap");
    if (mic_ret != 0) {
        LOG_ERR("mic_cap start failed: %d", mic_ret);
    }

    int up_ret = rt_thread_start(&g_mic_upload_thread,
                                 g_mic_upload_stack,
                                 K_THREAD_STACK_SIZEOF(g_mic_upload_stack),
                                 mic_upload_entry,
                                 NULL, NULL, NULL,
                                 MIC_UPLOAD_PRIO,
                                 0,
                                 "mic_up");
    if (up_ret != 0) {
        LOG_ERR("mic_up start failed: %d", up_ret);
    }
#endif

#ifdef CONFIG_SPK_STREAM
    k_sem_init(&g_spk_test_sem, 0, 1);
    (void)rt_thread_start(&g_spk_rx_thread,
                          g_spk_rx_stack,
                          K_THREAD_STACK_SIZEOF(g_spk_rx_stack),
                          spk_rx_entry,
                          NULL, NULL, NULL,
                          SPK_STREAM_PRIO,
                          0,
                          "spk_rx");

    (void)rt_thread_start(&g_spk_play_thread,
                          g_spk_play_stack,
                          K_THREAD_STACK_SIZEOF(g_spk_play_stack),
                          spk_play_entry,
                          NULL, NULL, NULL,
                          SPK_PLAY_PRIO,
                          0,
                          "spk_play");
#endif
}

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
#define AUDIO_MAX_FRAGS 30
#define AUDIO_CODEC_PCM16_LE 1
#define AUDIO_CODEC_IMA_ADPCM_8K 2
#define AUDIO_CODEC_HDR_LEN 8
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

static const int g_ima_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static const int8_t g_ima_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static uint8_t ima_adpcm_encode_nibble(int16_t sample, int16_t *pred, uint8_t *index)
{
    int step = g_ima_step_table[*index];
    int diff = (int)sample - (int)(*pred);
    uint8_t nibble = 0;
    int vpdiff = step >> 3;

    if (diff < 0) {
        nibble = 0x08;
        diff = -diff;
    }
    if (diff >= step) {
        nibble |= 0x04;
        diff -= step;
        vpdiff += step;
    }
    step >>= 1;
    if (diff >= step) {
        nibble |= 0x02;
        diff -= step;
        vpdiff += step;
    }
    step >>= 1;
    if (diff >= step) {
        nibble |= 0x01;
        vpdiff += step;
    }

    if (nibble & 0x08) {
        *pred = (int16_t)MAX((int)(*pred) - vpdiff, -32768);
    } else {
        *pred = (int16_t)MIN((int)(*pred) + vpdiff, 32767);
    }

    int idx = (int)(*index) + g_ima_index_table[nibble & 0x0F];
    if (idx < 0) {
        idx = 0;
    } else if (idx > 88) {
        idx = 88;
    }
    *index = (uint8_t)idx;
    return (uint8_t)(nibble & 0x0F);
}

static size_t audio_encode_ima_adpcm_8k(const int16_t *pcm16,
                                        size_t pcm_samples,
                                        uint8_t *out,
                                        size_t out_cap)
{
    if (pcm16 == NULL || out == NULL || pcm_samples < 2U || out_cap < (AUDIO_CODEC_HDR_LEN + 1U)) {
        return 0U;
    }

    static int16_t s_ds[320];
    size_t ds_n = 0U;
    for (size_t i = 0; i < pcm_samples && ds_n < ARRAY_SIZE(s_ds); i += 2U) {
        s_ds[ds_n++] = pcm16[i];
    }
    if (ds_n < 2U || ds_n > 255U) {
        return 0U;
    }

    size_t nibble_cnt = ds_n - 1U;
    size_t data_bytes = (nibble_cnt + 1U) / 2U;
    size_t total = AUDIO_CODEC_HDR_LEN + data_bytes;
    if (total > out_cap) {
        return 0U;
    }

    int16_t pred = s_ds[0];
    uint8_t idx = 0U;

    out[0] = AUDIO_CODEC_IMA_ADPCM_8K;
    out[1] = 8U;   /* sample rate kHz */
    out[2] = 1U;   /* mono */
    out[3] = 4U;   /* bits per sample */
    sys_put_le16((uint16_t)pred, &out[4]);
    out[6] = idx;
    out[7] = (uint8_t)ds_n;

    size_t out_i = AUDIO_CODEC_HDR_LEN;
    bool low = true;
    uint8_t packed = 0U;
    for (size_t i = 1U; i < ds_n; i++) {
        uint8_t n = ima_adpcm_encode_nibble(s_ds[i], &pred, &idx);
        if (low) {
            packed = n;
            low = false;
        } else {
            packed |= (uint8_t)(n << 4);
            out[out_i++] = packed;
            packed = 0U;
            low = true;
        }
    }
    if (!low) {
        out[out_i++] = packed;
    }

    out[6] = idx;
    return out_i;
}

#ifdef CONFIG_SPK_STREAM
#define SPK_STREAM_STACK_SIZE 4096
#define SPK_STREAM_PRIO 4
#define SPK_PLAY_STACK_SIZE 2048
#define SPK_PLAY_PRIO 5

#define SPK_FRAME_SAMPLES 320U /* 20 ms @ 16 kHz */
#define SPK_FRAME_BYTES (SPK_FRAME_SAMPLES * sizeof(int16_t))
#define SPK_FRAME_Q_LEN 24
#define SPK_BUF_LOW_WATER 2
#define SPK_PREBUFFER_FRAMES 6
#define SPK_TEST_BUF_MAX (32 * 1024)

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
        if (app_uplink_service_is_ready()) {
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

static void spk_rx_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    size_t stream_len = 0;
    uint32_t rx_pkts = 0;
    uint32_t rx_audio_pkts = 0;
    uint32_t rx_ctrl_pkts = 0;
    uint32_t rx_bad_hdr = 0;
    uint32_t rx_drop_buf = 0;
    uint32_t rx_overflow = 0;
    uint32_t rx_empty_payload = 0;
    uint16_t last_seq = 0;
    bool last_seq_valid = false;
    uint16_t asm_seq = 0;
    bool asm_valid = false;
    uint8_t asm_frag_cnt = 0;
    uint8_t asm_frag_seen = 0;
    uint32_t asm_frag_mask = 0;
    size_t asm_len = 0;
    uint8_t asm_buf[SPK_FRAME_BYTES * 2]; /* enough for one 20ms frame (640B) */
    int64_t last_log = 0;
    int64_t last_audio_ms = 0;

    while (1) {
        app_data_record_t dl_rec;
        int r = app_uplink_take_downlink(&dl_rec, 1000);
        if (r != HAL_OK) {
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
        const uint8_t *pkt = dl_rec.data;
        int pkt_len = (int)dl_rec.len;
        rx_pkts++;

        if (pkt_len <= 0 || pkt == NULL) {
            continue;
        }
        if (handle_ctrl_msg(pkt, pkt_len)) {
            rx_ctrl_pkts++;
            continue;
        }
        if (pkt_len < (int)sizeof(struct audio_pkt_hdr)) {
            rx_bad_hdr++;
            continue;
        }

        struct audio_pkt_hdr *hdr = (struct audio_pkt_hdr *)pkt;
        if (hdr->magic0 != AUDIO_PKT_MAGIC0 || hdr->magic1 != AUDIO_PKT_MAGIC1) {
            rx_bad_hdr++;
            continue;
        }
        if (!g_spk_accept_audio) {
            continue;
        }

        uint16_t payload_len = sys_get_le16((uint8_t *)&hdr->payload_len);
        uint16_t avail = (uint16_t)(pkt_len - sizeof(struct audio_pkt_hdr));
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
            rx_empty_payload++;
            continue;
        }

        rx_audio_pkts++;
        if (!last_seq_valid) {
            last_seq = hdr->seq;
            last_seq_valid = true;
        } else if ((uint16_t)(last_seq + 1U) != hdr->seq && hdr->frag_idx == 0) {
            LOG_WRN("SPK RX seq gap: last=%u curr=%u", (unsigned)last_seq, (unsigned)hdr->seq);
            last_seq = hdr->seq;
        } else if (hdr->frag_idx == 0) {
            last_seq = hdr->seq;
        }

        /* Reassemble fragments per seq; append only when full frame assembled. */
        if (hdr->frag_cnt > 0 && hdr->frag_cnt <= 30) {
            if (!asm_valid || asm_seq != hdr->seq) {
                /* New sequence: reset assembly state. */
                asm_valid = true;
                asm_seq = hdr->seq;
                asm_frag_cnt = hdr->frag_cnt;
                asm_frag_seen = 0;
                asm_frag_mask = 0;
                asm_len = 0;
            }
            if (hdr->frag_cnt != asm_frag_cnt) {
                /* Mismatch, drop current assembly. */
                asm_valid = false;
            } else if (hdr->frag_idx < 30U) {
                uint32_t bit = (1U << hdr->frag_idx);
                if ((asm_frag_mask & bit) == 0U) {
                    size_t off = (size_t)hdr->frag_idx * payload_len;
                    if (off + payload_len <= sizeof(asm_buf)) {
                        memcpy(asm_buf + off, pkt + sizeof(struct audio_pkt_hdr), payload_len);
                        asm_frag_mask |= bit;
                        asm_frag_seen++;
                        if (off + payload_len > asm_len) {
                            asm_len = off + payload_len;
                        }
                    }
                }
            }
            if (asm_valid && asm_frag_seen == asm_frag_cnt) {
                size_t space = sizeof(g_spk_test_buf) - g_spk_test_len;
                if (space >= asm_len) {
                    memcpy(g_spk_test_buf + g_spk_test_len, asm_buf, asm_len);
                    g_spk_test_len += asm_len;
                } else {
                    g_spk_test_overflow++;
                    rx_drop_buf++;
                }
                asm_valid = false;
            }
        }
        int64_t now = k_uptime_get();
        last_audio_ms = now;
        if (now - last_log >= 1000) {
            last_log = now;
            LOG_INF("SPK RX bytes=%u overflow=%u pkts=%u audio=%u ctrl=%u bad=%u drop=%u empty=%u",
                    (unsigned)g_spk_test_len,
                    (unsigned)g_spk_test_overflow,
                    (unsigned)rx_pkts,
                    (unsigned)rx_audio_pkts,
                    (unsigned)rx_ctrl_pkts,
                    (unsigned)rx_bad_hdr,
                    (unsigned)rx_drop_buf,
                    (unsigned)rx_empty_payload);
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
        if (app_uplink_service_is_ready()) {
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
            if (used <= SPK_BUF_LOW_WATER && app_uplink_service_is_ready()) {
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
            if (app_uplink_service_is_ready()) {
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

    uint32_t frames = 0;
    int32_t peak_l = 0;
    int32_t peak_r = 0;
    uint64_t sum_sq_l = 0;
    uint64_t sum_sq_r = 0;
    uint32_t sample_cnt = 0;

    while (1) {
        void *buf = NULL;
        size_t len = 0;
        int r = hal_mic_read_block(&buf, &len, 1000);
        if (r == HAL_OK && buf != NULL && len > 0) {
            int16_t *pcm = (int16_t *)buf;
            size_t samples = len / sizeof(int16_t);
            for (size_t i = 0; i + 1 < samples; i += 2) {
                int32_t l = pcm[i];
                int32_t r = pcm[i + 1];
                int32_t al = l < 0 ? -l : l;
                int32_t ar = r < 0 ? -r : r;
                if (al > peak_l) {
                    peak_l = al;
                }
                if (ar > peak_r) {
                    peak_r = ar;
                }
                sum_sq_l += (uint64_t)(l * (int64_t)l);
                sum_sq_r += (uint64_t)(r * (int64_t)r);
            }
            sample_cnt += (uint32_t)(samples / 2U);
            frames++;
            (void)hal_mic_release(buf);
        }

        uint64_t now = k_uptime_get();
        if (now - last_log >= 1000) {
            uint32_t rms_l = 0;
            uint32_t rms_r = 0;
            if (sample_cnt > 0) {
                rms_l = (uint32_t)(sum_sq_l / sample_cnt);
                rms_r = (uint32_t)(sum_sq_r / sample_cnt);
            }
            LOG_INF("MIC level: frames=%u L_peak=%d L_rms=%u | R_peak=%d R_rms=%u",
                    frames, (int)peak_l, rms_l, (int)peak_r, rms_r);
            last_log = now;
            peak_l = 0;
            peak_r = 0;
            sum_sq_l = 0;
            sum_sq_r = 0;
            sample_cnt = 0;
            frames = 0;
        }
    }
}
#endif

#if defined(CONFIG_SPK_BOOT_TONE) && !defined(CONFIG_BOOT_TONE)
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
#define BLE_TEST_STACK_SIZE 2048
#define BLE_TEST_PRIO 3

static struct k_thread g_ble_test_thread;
RT_THREAD_STACK_DEFINE(g_ble_test_stack, BLE_TEST_STACK_SIZE);

static void ble_test_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("BLE test thread started (via uplink gateway)");

    while (1) {
        app_data_record_t rec;
        int r = app_uplink_take_downlink(&rec, 1000);
        if (r == HAL_OK) {
            LOG_INF("BLE RX %u bytes part=%d", (unsigned)rec.len, (int)rec.part);
        }
    }
}
#endif

#ifdef CONFIG_MIC_TEST
#include "hal_mic.h"
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

    uint8_t frame_payload[sizeof(struct audio_pkt_hdr) + AUDIO_PAYLOAD_MAX];
    size_t payload_len = 0U;
    if (len >= sizeof(int16_t) * 2U) {
        payload_len = audio_encode_ima_adpcm_8k((const int16_t *)pcm,
                                                len / sizeof(int16_t),
                                                frame_payload,
                                                sizeof(frame_payload));
    }
    if (payload_len == 0U) {
        if ((len + 1U) > sizeof(frame_payload)) {
            return HAL_EINVAL;
        }
        frame_payload[0] = AUDIO_CODEC_PCM16_LE;
        memcpy(&frame_payload[1], pcm, len);
        payload_len = len + 1U;
    }

    size_t uplink_max = app_uplink_max_payload();
    size_t payload_max = AUDIO_PAYLOAD_MAX;
    if (uplink_max > sizeof(struct audio_pkt_hdr)) {
        size_t dyn_max = uplink_max - sizeof(struct audio_pkt_hdr);
        if (dyn_max < payload_max) {
            payload_max = dyn_max;
        }
    } else {
        return HAL_EBUSY;
    }

    uint16_t frame_seq = seq;
    uint8_t frag_cnt = (uint8_t)((payload_len + payload_max - 1) / payload_max);
    if (frag_cnt == 0U || frag_cnt > AUDIO_MAX_FRAGS) {
        return HAL_EINVAL;
    }

    static uint8_t s_pkts[AUDIO_MAX_FRAGS][sizeof(struct audio_pkt_hdr) + AUDIO_PAYLOAD_MAX];
    static const uint8_t *s_pkt_ptrs[AUDIO_MAX_FRAGS];
    static uint16_t s_pkt_lens[AUDIO_MAX_FRAGS];
    size_t offset = 0U;

    for (uint8_t frag = 0; frag < frag_cnt; frag++) {
        size_t remaining = payload_len - offset;
        size_t chunk = (remaining > payload_max) ? payload_max : remaining;

        uint8_t *pkt = s_pkts[frag];
        struct audio_pkt_hdr *hdr = (struct audio_pkt_hdr *)pkt;
        hdr->magic0 = AUDIO_PKT_MAGIC0;
        hdr->magic1 = AUDIO_PKT_MAGIC1;
        sys_put_le16(frame_seq, (uint8_t *)&hdr->seq);
        hdr->frag_idx = frag;
        hdr->frag_cnt = frag_cnt;
        sys_put_le16((uint16_t)chunk, (uint8_t *)&hdr->payload_len);

        memcpy(pkt + sizeof(struct audio_pkt_hdr), frame_payload + offset, chunk);
        s_pkt_ptrs[frag] = pkt;
        s_pkt_lens[frag] = (uint16_t)(sizeof(struct audio_pkt_hdr) + chunk);

        offset += chunk;
    }

    return app_uplink_publish_batch(APP_DATA_PART_AUDIO_UP,
                                    APP_UPLINK_PRIO_LOW,
                                    s_pkt_ptrs,
                                    s_pkt_lens,
                                    frag_cnt,
                                    (uint32_t)k_uptime_get());
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
    uint32_t drop_not_ready = 0;
    uint32_t drop_send_fail = 0;
    uint32_t pkts_sent = 0;
    uint32_t pcm_bytes_sent = 0;
    uint32_t wire_bytes_sent = 0;
    int32_t peak = 0;
    uint64_t sum_sq = 0;
    uint32_t sample_cnt = 0;

    while (1) {
        struct mic_frame_msg msg;
        int q = k_msgq_get(&g_mic_frame_q, &msg, K_MSEC(1000));
        if (q != 0) {
            continue;
        }

        frames++;

        if (!app_state_mic_enabled()) {
            frames_drop++;
            drop_not_ready++;
            (void)hal_mic_release(msg.buf);
            goto log_stats;
        }

        /* Real-time path: do not backlog audio before uplink transport is ready. */
        if (!app_uplink_service_is_ready() ||
            app_uplink_max_payload() <= sizeof(struct audio_pkt_hdr)) {
            frames_drop++;
            drop_not_ready++;
            (void)hal_mic_release(msg.buf);
            goto log_stats;
        }

        size_t payload_max = AUDIO_PAYLOAD_MAX;
        size_t uplink_max = app_uplink_max_payload();
        if (uplink_max > sizeof(struct audio_pkt_hdr)) {
            size_t dyn_max = uplink_max - sizeof(struct audio_pkt_hdr);
            if (dyn_max < payload_max) {
                payload_max = dyn_max;
            }
        }

        {
            const int16_t *pcm = (const int16_t *)msg.buf;
            size_t samples = msg.len / sizeof(int16_t);
            for (size_t i = 0; i < samples; i++) {
                int32_t v = pcm[i];
                int32_t av = v < 0 ? -v : v;
                if (av > peak) {
                    peak = av;
                }
                sum_sq += (uint64_t)(v * (int64_t)v);
            }
            sample_cnt += (uint32_t)samples;
        }

        int ret = audio_send_frame((const uint8_t *)msg.buf, msg.len, msg.seq);
        if (ret == HAL_OK) {
            frames_sent++;
            pkts_sent += (uint32_t)((msg.len + payload_max - 1) / payload_max);
            pcm_bytes_sent += (uint32_t)msg.len;
            if (msg.len >= sizeof(int16_t) * 2U) {
                size_t ds_n = (msg.len / sizeof(int16_t) + 1U) / 2U;
                size_t nibble_cnt = (ds_n > 0U) ? (ds_n - 1U) : 0U;
                wire_bytes_sent += (uint32_t)(AUDIO_CODEC_HDR_LEN + ((nibble_cnt + 1U) / 2U));
            } else {
                wire_bytes_sent += (uint32_t)(msg.len + 1U);
            }
        } else {
            frames_drop++;
            drop_send_fail++;
        }

        (void)hal_mic_release(msg.buf);

log_stats:
        {
            uint64_t now = k_uptime_get();
            if (now - last_log >= 5000) {
                uint32_t rms = 0;
                if (sample_cnt > 0) {
                    rms = (uint32_t)(sum_sq / sample_cnt);
                }
#if !IS_ENABLED(CONFIG_PPG_TUNE_MODE)
                LOG_INF("MIC stream: frames=%u sent=%u drop=%u nrdy=%u send=%u pkts=%u pcm=%u wire=%u ready=%d max_tx=%u",
                        frames, frames_sent, frames_drop, drop_not_ready, drop_send_fail,
                        pkts_sent, pcm_bytes_sent, wire_bytes_sent,
                        app_uplink_service_is_ready(),
                        (unsigned)app_uplink_max_payload());
#endif
                LOG_INF("MIC_UP: peak=%d rms=%u samples=%u", (int)peak, rms, (unsigned)sample_cnt);
                last_log = now;
                peak = 0;
                sum_sq = 0;
                sample_cnt = 0;
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

#if defined(CONFIG_SPK_BOOT_TONE) && !defined(CONFIG_BOOT_TONE)
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

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/byteorder.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arm_math.h>

#include "app_rtc.h"
#include "app_state.h"
#include "app_imu_test.h"
#include "app_gps.h"
#include "app_ppg_hr.h"
#include "hal_audio.h"
#include "hal_spk.h"
#include "hal_mic.h"
#include "error.h"
#include "spk_postproc.h"

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

/* Uplink audio DSP (lightweight) */
#define MIC_DSP_PRE_GAIN      1.0f
#define MIC_DSP_DC_R          0.995f
#define MIC_DSP_GATE_TH       0.0f
#define MIC_DSP_LIMIT         30000.0f

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

static size_t audio_encode_ima_adpcm_16k(const int16_t *pcm16,
                                         size_t pcm_samples,
                                         uint8_t *out,
                                         size_t out_cap)
{
    if (pcm16 == NULL || out == NULL || pcm_samples < 2U || out_cap < (AUDIO_CODEC_HDR_LEN + 1U)) {
        return 0U;
    }

    if (pcm_samples < 2U || pcm_samples > 255U) {
        return 0U;
    }

    size_t nibble_cnt = pcm_samples - 1U;
    size_t data_bytes = (nibble_cnt + 1U) / 2U;
    size_t total = AUDIO_CODEC_HDR_LEN + data_bytes;
    if (total > out_cap) {
        return 0U;
    }

    int16_t pred = pcm16[0];
    uint8_t idx = 0U;

    out[0] = AUDIO_CODEC_IMA_ADPCM_8K;
    out[1] = 16U;  /* sample rate kHz */
    out[2] = 1U;   /* mono */
    out[3] = 4U;   /* bits per sample */
    sys_put_le16((uint16_t)pred, &out[4]);
    out[6] = idx;
    out[7] = (uint8_t)pcm_samples;

    size_t out_i = AUDIO_CODEC_HDR_LEN;
    bool low = true;
    uint8_t packed = 0U;
    int16_t prev = pcm16[0];
    for (size_t i = 1U; i < pcm_samples; i++) {
        prev = pcm16[i];
        uint8_t n = ima_adpcm_encode_nibble(prev, &pred, &idx);
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

static void mic_dsp_process(int16_t *pcm, size_t samples)
{
    /* Simple DC blocker + gain + noise gate + soft limiter */
    static float dc_x1 = 0.0f;
    static float dc_y1 = 0.0f;

    for (size_t i = 0; i < samples; i++) {
        float x = (float)pcm[i];
        float y = x - dc_x1 + MIC_DSP_DC_R * dc_y1;
        dc_x1 = x;
        dc_y1 = y;

        y *= MIC_DSP_PRE_GAIN;

        if (MIC_DSP_GATE_TH > 0.0f) {
            float ay = y >= 0.0f ? y : -y;
            if (ay < MIC_DSP_GATE_TH) {
                float t = ay / MIC_DSP_GATE_TH;
                float gain = t * t;
                y *= gain;
            }
        }

        if (y > MIC_DSP_LIMIT) {
            y = MIC_DSP_LIMIT + (y - MIC_DSP_LIMIT) * 0.5f;
        } else if (y < -MIC_DSP_LIMIT) {
            y = -MIC_DSP_LIMIT + (y + MIC_DSP_LIMIT) * 0.5f;
        }

        if (y > 32767.0f) y = 32767.0f;
        if (y < -32768.0f) y = -32768.0f;

        pcm[i] = (int16_t)y;
    }
}

#ifdef CONFIG_SPK_STREAM
#define SPK_STREAM_STACK_SIZE 4096
#define SPK_STREAM_PRIO 4
#define SPK_PLAY_STACK_SIZE 2048
#define SPK_PLAY_PRIO 3

#define SPK_FRAME_SAMPLES 160U /* 10 ms @ 16 kHz */
#define SPK_FRAME_BYTES (SPK_FRAME_SAMPLES * sizeof(int16_t))
#define SPK_ENC_FRAME_MAX 88U
#define SPK_RING_FRAMES 512U               /* ~51 KB for ADPCM frames */
#define SPK_HIGH_WATER_FRAMES 40U          /* ~400 ms */
#define SPK_LOW_WATER_FRAMES 16U           /* ~160 ms */
#define SPK_STREAM_RATE_MIN_PM 980U        /* ~0.98x realtime */
#define SPK_FORCE_START_FRAMES 96U         /* ~960 ms backlog */
#define SPK_FORCE_START_WAIT_MS 700U

struct spk_enc_slot {
    uint8_t len;
    uint8_t data[SPK_ENC_FRAME_MAX];
};

static struct k_thread g_spk_rx_thread;
RT_THREAD_STACK_DEFINE(g_spk_rx_stack, SPK_STREAM_STACK_SIZE);

static struct k_thread g_spk_play_thread;
RT_THREAD_STACK_DEFINE(g_spk_play_stack, SPK_PLAY_STACK_SIZE);
static volatile bool g_spk_buf_full = false;
static volatile bool g_spk_accept_audio = false;
static volatile bool g_nrf_ready_sent = false;
static volatile bool g_playback_critical = false;
#if defined(CONFIG_MIC_TEST) && (DOWNLINK_TEST == 0)
static struct k_thread g_mic_test_thread;
static struct k_thread g_mic_upload_thread;
#endif
static bool g_mic_cap_started = false;
static bool g_mic_up_started = false;
static bool g_mic_cap_paused = false;
static bool g_mic_up_paused = false;
static volatile bool g_mic_pause_req = false;
static uint32_t g_spk_play_frames = 0;
static uint32_t g_spk_play_errs = 0;
static struct spk_enc_slot g_spk_ring[SPK_RING_FRAMES];
static struct k_mutex g_spk_ring_lock;
static size_t g_spk_ring_ridx = 0U;
static size_t g_spk_ring_widx = 0U;
static size_t g_spk_ring_count = 0U;
static bool g_spk_eos_received = false;
static bool g_spk_rx_reset = false;
static uint32_t g_spk_rx_audio_frames = 0U;
static int64_t g_spk_rx_start_ms = 0;
static uint8_t g_spk_asm_buf[SPK_FRAME_BYTES * 2];
static uint8_t g_spk_asm_part_buf[AUDIO_MAX_FRAGS][AUDIO_PAYLOAD_MAX];
static uint16_t g_spk_asm_part_len[AUDIO_MAX_FRAGS];
static uint8_t g_spk_play_buf[SPK_FRAME_BYTES * 2];
static uint32_t g_spk_play_sid = 0U;
static int64_t g_spk_t_ctrl_ms = 0;
static int64_t g_spk_t_first_audio_ms = 0;
static int64_t g_spk_t_start_ms = 0;
static int64_t g_spk_t_eos_ms = 0;
static int64_t g_spk_t_done_ms = 0;

static void mic_drop_queue(void);

static size_t spk_make_gap_fill_frame(int16_t *dst,
                                      const int16_t *last_pcm,
                                      size_t samples,
                                      uint32_t gap_index)
{
    if (dst == NULL || last_pcm == NULL || samples == 0U) {
        return 0U;
    }

    /* For short underruns, emit only a tiny decaying tail instead of
     * repeating the last voiced frame, which produces an audible buzz. */
    static const uint32_t k_gap_gains_q15[] = {
        12288U, /* 0.375 */
        6144U,  /* 0.1875 */
        3072U,  /* 0.09375 */
        1536U,  /* 0.046875 */
    };
    if (gap_index >= ARRAY_SIZE(k_gap_gains_q15)) {
        return 0U;
    }
    uint32_t base_q15 = k_gap_gains_q15[gap_index];

    for (size_t i = 0; i < samples; i++) {
        uint32_t fade_q15 = (uint32_t)(((samples - i) * 32768U) / (samples + 1U));
        int32_t v = (int32_t)last_pcm[i];
        v = (v * (int32_t)base_q15) >> 15;
        v = (v * (int32_t)fade_q15) >> 15;
        dst[i] = (int16_t)v;
    }

    return samples * sizeof(int16_t);
}

static void spk_ring_reset(void)
{
    k_mutex_lock(&g_spk_ring_lock, K_FOREVER);
    g_spk_ring_ridx = 0U;
    g_spk_ring_widx = 0U;
    g_spk_ring_count = 0U;
    g_spk_eos_received = false;
    g_spk_rx_audio_frames = 0U;
    g_spk_rx_start_ms = 0;
    k_mutex_unlock(&g_spk_ring_lock);
}

static bool spk_ring_push(const uint8_t *data, size_t len)
{
    bool ok = false;
    if (data == NULL || len == 0U || len > SPK_ENC_FRAME_MAX) {
        return false;
    }
    k_mutex_lock(&g_spk_ring_lock, K_FOREVER);
    if (g_spk_ring_count < SPK_RING_FRAMES) {
        struct spk_enc_slot *slot = &g_spk_ring[g_spk_ring_widx];
        slot->len = (uint8_t)len;
        memcpy(slot->data, data, len);
        g_spk_ring_widx = (g_spk_ring_widx + 1U) % SPK_RING_FRAMES;
        g_spk_ring_count++;
        ok = true;
    }
    k_mutex_unlock(&g_spk_ring_lock);
    return ok;
}

static bool spk_ring_pop(uint8_t *out, size_t out_cap, size_t *out_len)
{
    bool ok = false;
    if (out == NULL || out_len == NULL) {
        return false;
    }
    *out_len = 0U;
    k_mutex_lock(&g_spk_ring_lock, K_FOREVER);
    if (g_spk_ring_count > 0U) {
        struct spk_enc_slot *slot = &g_spk_ring[g_spk_ring_ridx];
        if (slot->len > 0U && slot->len <= out_cap) {
            memcpy(out, slot->data, slot->len);
            *out_len = slot->len;
            g_spk_ring_ridx = (g_spk_ring_ridx + 1U) % SPK_RING_FRAMES;
            g_spk_ring_count--;
            ok = true;
        }
    }
    k_mutex_unlock(&g_spk_ring_lock);
    return ok;
}

static size_t spk_ring_count(void)
{
    size_t count;
    k_mutex_lock(&g_spk_ring_lock, K_FOREVER);
    count = g_spk_ring_count;
    k_mutex_unlock(&g_spk_ring_lock);
    return count;
}

static void spk_mark_eos(void)
{
    k_mutex_lock(&g_spk_ring_lock, K_FOREVER);
    g_spk_eos_received = true;
    k_mutex_unlock(&g_spk_ring_lock);
    if (g_spk_t_eos_ms == 0) {
        g_spk_t_eos_ms = k_uptime_get();
    }
}

static bool spk_is_eos_received(void)
{
    bool eos;
    k_mutex_lock(&g_spk_ring_lock, K_FOREVER);
    eos = g_spk_eos_received;
    k_mutex_unlock(&g_spk_ring_lock);
    return eos;
}

static uint32_t spk_rx_rate_permille(void)
{
    uint32_t rate = 0U;
    k_mutex_lock(&g_spk_ring_lock, K_FOREVER);
    int64_t now = k_uptime_get();
    int64_t elapsed = now - g_spk_rx_start_ms;
    if (g_spk_rx_start_ms > 0 && elapsed > 0) {
        rate = (uint32_t)((g_spk_rx_audio_frames * 10000ULL) / (uint64_t)elapsed);
    }
    k_mutex_unlock(&g_spk_ring_lock);
    return rate;
}

void app_rtc_playback_critical_enter(void)
{
    if (g_playback_critical) {
        return;
    }
    g_playback_critical = true;

    g_mic_pause_req = true;
    mic_drop_queue();
    (void)hal_mic_stop();

    if (IS_ENABLED(CONFIG_IMU_TEST)) {
        app_imu_test_pause();
    }
    if (IS_ENABLED(CONFIG_GPS_TEST)) {
        app_gps_pause();
    }
    if (IS_ENABLED(CONFIG_PPG_SPI_PROBE)) {
        app_ppg_hr_pause();
    }
}

void app_rtc_playback_critical_exit(void)
{
    if (!g_playback_critical) {
        return;
    }

    if (IS_ENABLED(CONFIG_PPG_SPI_PROBE)) {
        app_ppg_hr_resume();
    }
    if (IS_ENABLED(CONFIG_GPS_TEST)) {
        app_gps_resume();
    }
    if (IS_ENABLED(CONFIG_IMU_TEST)) {
        app_imu_test_resume();
    }

    g_mic_pause_req = false;

    g_playback_critical = false;
}

static int16_t ima_adpcm_decode_nibble(uint8_t nibble, int16_t *pred, uint8_t *index)
{
    int step = g_ima_step_table[*index];
    int diff = step >> 3;

    if (nibble & 0x04) {
        diff += step;
    }
    if (nibble & 0x02) {
        diff += step >> 1;
    }
    if (nibble & 0x01) {
        diff += step >> 2;
    }

    if (nibble & 0x08) {
        *pred = (int16_t)MAX((int)(*pred) - diff, -32768);
    } else {
        *pred = (int16_t)MIN((int)(*pred) + diff, 32767);
    }

    int idx = (int)(*index) + g_ima_index_table[nibble & 0x0F];
    if (idx < 0) {
        idx = 0;
    } else if (idx > 88) {
        idx = 88;
    }
    *index = (uint8_t)idx;
    return *pred;
}

static int audio_decode_frame_to_pcm16(const uint8_t *in,
                                       size_t in_len,
                                       int16_t *out,
                                       size_t out_cap_samples,
                                       size_t *out_samples,
                                       uint32_t *out_rate_hz)
{
    if (in == NULL || out == NULL || out_samples == NULL || out_rate_hz == NULL ||
        in_len == 0U || out_cap_samples == 0U) {
        return HAL_EINVAL;
    }

    uint8_t codec = in[0];
    if (codec == AUDIO_CODEC_PCM16_LE) {
        size_t pcm_bytes = in_len - 1U;
        size_t samples = pcm_bytes / sizeof(int16_t);
        if ((pcm_bytes % sizeof(int16_t)) != 0U || samples > out_cap_samples) {
            return HAL_EINVAL;
        }
        memcpy(out, &in[1], samples * sizeof(int16_t));
        *out_samples = samples;
        *out_rate_hz = 16000U;
        return HAL_OK;
    }

    if (codec != AUDIO_CODEC_IMA_ADPCM_8K || in_len < (AUDIO_CODEC_HDR_LEN + 1U)) {
        return HAL_EINVAL;
    }

    uint8_t sample_rate_khz = in[1];
    uint8_t sample_count = in[7];
    if (sample_count < 2U) {
        return HAL_EINVAL;
    }

    size_t need_out_samples = sample_count;
    if (sample_rate_khz != 8U && sample_rate_khz != 16U) {
        return HAL_EINVAL;
    }

    if (need_out_samples > out_cap_samples) {
        return HAL_EINVAL;
    }

    size_t nibble_cnt = sample_count - 1U;
    size_t need_bytes = (nibble_cnt + 1U) / 2U;
    if (in_len < (AUDIO_CODEC_HDR_LEN + need_bytes)) {
        return HAL_EINVAL;
    }

    int16_t pred = (int16_t)sys_get_le16(&in[4]);
    uint8_t idx = in[6];
    if (idx > 88U) {
        idx = 88U;
    }

    out[0] = pred;
    size_t out_i = 1U;
    for (size_t i = 0U; i < need_bytes && out_i < need_out_samples; i++) {
        uint8_t packed = in[AUDIO_CODEC_HDR_LEN + i];
        out[out_i++] = ima_adpcm_decode_nibble((uint8_t)(packed & 0x0F), &pred, &idx);
        if (out_i < need_out_samples) {
            out[out_i++] = ima_adpcm_decode_nibble((uint8_t)((packed >> 4) & 0x0F), &pred, &idx);
        }
    }

    *out_samples = out_i;
    *out_rate_hz = (uint32_t)sample_rate_khz * 1000U;
    return HAL_OK;
}

static bool handle_ctrl_msg(const uint8_t *buf, int len)
{
    if (len <= 0 || buf == NULL) {
        return false;
    }
    if (len >= 12 && memcmp(buf, "APP_SPK_VOL:", 12) == 0) {
        char tmp[5] = {0};
        int n = len - 12;
        if (n > 3) {
            n = 3;
        }
        if (n > 0) {
            memcpy(tmp, buf + 12, (size_t)n);
            int vol = atoi(tmp);
            if (vol < 0) {
                vol = 0;
            } else if (vol > 500) {
                vol = 500;
            }
            spk_postproc_set_volume_percent((uint16_t)vol);
            LOG_INF("CTRL APP_SPK_VOL=%d", vol);
            return true;
        }
    }
    if (len >= 14 && memcmp(buf, "APP_PLAY_START", 14) == 0) {
        LOG_INF("CTRL APP_PLAY_START");
        g_spk_play_sid++;
        g_spk_t_ctrl_ms = k_uptime_get();
        g_spk_t_first_audio_ms = 0;
        g_spk_t_start_ms = 0;
        g_spk_t_eos_ms = 0;
        g_spk_t_done_ms = 0;
        app_rtc_playback_critical_enter();
        g_spk_accept_audio = true;
        g_spk_buf_full = false;
        app_state_set(AUDIO_MODE_PLAY);
        g_nrf_ready_sent = false;
        mic_drop_queue();
        return true;
    }
    if (len >= 10 && memcmp(buf, "APP_READY?", 10) == 0) {
        LOG_INF("CTRL APP_READY?");
        g_spk_accept_audio = false;
        g_nrf_ready_sent = false;
        g_spk_buf_full = false;
        g_spk_rx_reset = true;
        spk_ring_reset();
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
        spk_mark_eos();
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

    uint32_t rx_pkts = 0;
    uint32_t rx_audio_pkts = 0;
    uint32_t rx_ctrl_pkts = 0;
    uint32_t rx_bad_hdr = 0;
    uint32_t rx_drop_buf = 0;
    uint32_t rx_decode_err = 0;
    uint32_t rx_overflow = 0;
    uint32_t rx_empty_payload = 0;
    uint16_t last_seq = 0;
    bool last_seq_valid = false;
    uint16_t asm_seq = 0;
    bool asm_valid = false;
    uint8_t asm_frag_cnt = 0;
    uint8_t asm_frag_seen = 0;
    uint32_t asm_frag_mask = 0;
    size_t asm_len = 0U;
    int64_t last_log = 0;
    int64_t last_audio_ms = 0;

    while (1) {
        app_data_record_t dl_rec;
        int r = app_uplink_take_downlink(&dl_rec, 1000);
        if (r != HAL_OK) {
            continue;
        }
        const uint8_t *pkt = dl_rec.data;
        int pkt_len = (int)dl_rec.len;
        rx_pkts++;

        if (pkt_len <= 0 || pkt == NULL) {
            continue;
        }
        if (handle_ctrl_msg(pkt, pkt_len)) {
            if (g_spk_rx_reset) {
                last_seq_valid = false;
                asm_valid = false;
                asm_frag_cnt = 0U;
                asm_frag_seen = 0U;
                asm_frag_mask = 0U;
                asm_len = 0U;
                g_spk_rx_reset = false;
            }
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
        uint16_t payload_len = sys_get_le16((uint8_t *)&hdr->payload_len);
        uint16_t avail = (uint16_t)(pkt_len - sizeof(struct audio_pkt_hdr));
        if (payload_len > avail) {
            payload_len = avail;
        }

        if (hdr->frag_cnt == 0 && payload_len == 0) {
            spk_mark_eos();
            last_audio_ms = 0;
            continue;
        }
        if (!g_spk_accept_audio) {
            continue;
        }
        if (payload_len == 0) {
            rx_empty_payload++;
            continue;
        }

        rx_audio_pkts++;
        if (g_spk_t_first_audio_ms == 0) {
            g_spk_t_first_audio_ms = k_uptime_get();
        }
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
                asm_len = 0U;
                memset(g_spk_asm_part_len, 0, sizeof(g_spk_asm_part_len));
            }
            if (hdr->frag_cnt != asm_frag_cnt) {
                /* Mismatch, drop current assembly. */
                asm_valid = false;
            } else if (hdr->frag_idx < 30U) {
                uint32_t bit = (1U << hdr->frag_idx);
                if ((asm_frag_mask & bit) == 0U) {
                    if (payload_len <= AUDIO_PAYLOAD_MAX) {
                        memcpy(g_spk_asm_part_buf[hdr->frag_idx], pkt + sizeof(struct audio_pkt_hdr), payload_len);
                        g_spk_asm_part_len[hdr->frag_idx] = payload_len;
                        asm_frag_mask |= bit;
                        asm_frag_seen++;
                    }
                }
            }
            if (asm_valid && asm_frag_seen == asm_frag_cnt) {
                asm_len = 0U;
                for (uint8_t i = 0U; i < asm_frag_cnt; i++) {
                    if ((asm_len + g_spk_asm_part_len[i]) > sizeof(g_spk_asm_buf)) {
                        asm_valid = false;
                        break;
                    }
                    memcpy(g_spk_asm_buf + asm_len, g_spk_asm_part_buf[i], g_spk_asm_part_len[i]);
                    asm_len += g_spk_asm_part_len[i];
                }
            }
            if (asm_valid && asm_frag_seen == asm_frag_cnt) {
                if (asm_len == 0U || asm_len > SPK_ENC_FRAME_MAX) {
                    rx_decode_err++;
                } else if (!spk_ring_push(g_spk_asm_buf, asm_len)) {
                    rx_drop_buf++;
                    g_spk_buf_full = true;
                    if (app_uplink_service_is_ready()) {
                        const char full[] = "BUF_FULL";
                        (void)app_uplink_publish(APP_DATA_PART_CTRL,
                                                 APP_UPLINK_PRIO_HIGH,
                                                 full,
                                                 sizeof(full) - 1,
                                                 (uint32_t)k_uptime_get());
                    }
                } else {
                    k_mutex_lock(&g_spk_ring_lock, K_FOREVER);
                    if (g_spk_rx_start_ms == 0) {
                        g_spk_rx_start_ms = k_uptime_get();
                    }
                    g_spk_rx_audio_frames++;
                    size_t used = g_spk_ring_count;
                    if (!g_spk_buf_full && used >= (SPK_RING_FRAMES - 8U) &&
                        app_uplink_service_is_ready()) {
                        const char full[] = "BUF_FULL";
                        (void)app_uplink_publish(APP_DATA_PART_CTRL,
                                                 APP_UPLINK_PRIO_HIGH,
                                                 full,
                                                 sizeof(full) - 1,
                                                 (uint32_t)k_uptime_get());
                        g_spk_buf_full = true;
                    }
                    k_mutex_unlock(&g_spk_ring_lock);
                }
                asm_valid = false;
            }
        }
        int64_t now = k_uptime_get();
        last_audio_ms = now;
        if (now - last_log >= 1000) {
            last_log = now;
            LOG_INF("SPK RX q=%u pkts=%u audio=%u ctrl=%u bad=%u dec=%u drop=%u empty=%u rate=%u",
                    (unsigned)spk_ring_count(),
                    (unsigned)rx_pkts,
                    (unsigned)rx_audio_pkts,
                    (unsigned)rx_ctrl_pkts,
                    (unsigned)rx_bad_hdr,
                    (unsigned)rx_decode_err,
                    (unsigned)rx_drop_buf,
                    (unsigned)rx_empty_payload,
                    (unsigned)spk_rx_rate_permille());
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
    ret = spk_postproc_init();
    if (ret != HAL_OK) {
        LOG_ERR("spk postproc init failed: %d", ret);
        return;
    }

    int64_t last_log = 0;
    bool spk_running = false;
    bool clip_mode = false;
    bool stream_mode = false;
    bool have_last_pcm = false;
    uint32_t gap_fill_frames = 0U;
    uint32_t session_audio_frames = 0U;
    uint32_t session_gap_fill_total = 0U;
    uint32_t session_silence_fill_total = 0U;
    static uint8_t enc_frame[SPK_ENC_FRAME_MAX];
    static int16_t pcm_frame[SPK_FRAME_SAMPLES];
    static int16_t last_pcm[SPK_FRAME_SAMPLES];
    static const uint8_t silence_frame[SPK_FRAME_BYTES] = {0};
    spk_postproc_diag_t spk_diag;

    while (1) {
        if (!spk_running) {
            size_t used = spk_ring_count();
            bool eos = spk_is_eos_received();
            if (used == 0U) {
                k_sleep(K_MSEC(10));
                continue;
            }
            if (!clip_mode && !stream_mode) {
                uint32_t rate_pm = spk_rx_rate_permille();
                int64_t now = k_uptime_get();
                bool force_start = (used >= SPK_FORCE_START_FRAMES);
                bool waited_enough = (g_spk_t_ctrl_ms > 0) &&
                                     ((uint32_t)(now - g_spk_t_ctrl_ms) >= SPK_FORCE_START_WAIT_MS);
                if (eos) {
                    clip_mode = true;
                } else if (used >= SPK_HIGH_WATER_FRAMES &&
                           (rate_pm >= SPK_STREAM_RATE_MIN_PM || force_start || waited_enough)) {
                    stream_mode = true;
                } else {
                    k_sleep(K_MSEC(10));
                    continue;
                }
            }
            ret = hal_spk_start();
            if (ret != HAL_OK) {
                LOG_ERR("spk start failed: %d", ret);
                k_sleep(K_MSEC(20));
                continue;
            }
            if (g_spk_t_start_ms == 0) {
                g_spk_t_start_ms = k_uptime_get();
            }
            spk_postproc_reset();
            session_audio_frames = 0U;
            session_gap_fill_total = 0U;
            session_silence_fill_total = 0U;
            spk_running = true;
        }

        size_t out_len = 0U;
        for (int f = 0; f < 2; f++) {
            size_t enc_len = 0U;
            bool have_frame = spk_ring_pop(enc_frame, sizeof(enc_frame), &enc_len);
            if (have_frame) {
                size_t pcm_samples = 0U;
                uint32_t pcm_rate_hz = 0U;
                int dec = audio_decode_frame_to_pcm16(enc_frame,
                                                      enc_len,
                                                      pcm_frame,
                                                      SPK_FRAME_SAMPLES,
                                                      &pcm_samples,
                                                      &pcm_rate_hz);
                if (dec == HAL_OK && pcm_samples > 0U) {
                    size_t proc_samples = 0U;
                    bool frame_final = spk_is_eos_received() && spk_ring_count() == 0U;
                    int pp = spk_postproc_process_frame(pcm_frame,
                                                        pcm_samples,
                                                        pcm_rate_hz,
                                                        (int16_t *)(g_spk_play_buf + out_len),
                                                        SPK_FRAME_SAMPLES,
                                                        &proc_samples,
                                                        frame_final);
                    if (pp == HAL_OK && proc_samples > 0U) {
                        size_t pcm_bytes = proc_samples * sizeof(int16_t);
                        memcpy(last_pcm, g_spk_play_buf + out_len, pcm_bytes);
                        have_last_pcm = true;
                        gap_fill_frames = 0U;
                        session_audio_frames += (uint32_t)(pcm_bytes / SPK_FRAME_BYTES);
                        out_len += pcm_bytes;
                    } else {
                        memcpy(g_spk_play_buf + out_len, silence_frame, SPK_FRAME_BYTES);
                        gap_fill_frames = 0U;
                        session_silence_fill_total++;
                        out_len += SPK_FRAME_BYTES;
                    }
                } else {
                    memcpy(g_spk_play_buf + out_len, silence_frame, SPK_FRAME_BYTES);
                    gap_fill_frames = 0U;
                    session_silence_fill_total++;
                    out_len += SPK_FRAME_BYTES;
                }
            } else if (spk_is_eos_received()) {
                break;
            } else if (have_last_pcm && stream_mode) {
                size_t plc_bytes = spk_make_gap_fill_frame((int16_t *)(g_spk_play_buf + out_len),
                                                           last_pcm,
                                                           SPK_FRAME_SAMPLES,
                                                           gap_fill_frames);
                if (plc_bytes > 0U) {
                    out_len += plc_bytes;
                    gap_fill_frames++;
                    session_gap_fill_total++;
                } else {
                    memcpy(g_spk_play_buf + out_len, silence_frame, SPK_FRAME_BYTES);
                    have_last_pcm = false;
                    gap_fill_frames = 0U;
                    session_silence_fill_total++;
                    out_len += SPK_FRAME_BYTES;
                }
            } else {
                memcpy(g_spk_play_buf + out_len, silence_frame, SPK_FRAME_BYTES);
                gap_fill_frames = 0U;
                session_silence_fill_total++;
                out_len += SPK_FRAME_BYTES;
            }
        }

        if (out_len > 0U) {
            int w = hal_spk_write(g_spk_play_buf, out_len, 1000);
            if (w != HAL_OK) {
                g_spk_play_errs++;
                LOG_ERR("spk write err: %d", w);
                (void)hal_spk_stop();
                spk_running = false;
            } else {
                g_spk_play_frames += (uint32_t)(out_len / SPK_FRAME_BYTES);
            }
        } else if (spk_is_eos_received() && spk_ring_count() == 0U) {
            (void)hal_spk_stop();
            spk_running = false;
        }

        int64_t now = k_uptime_get();
        if (now - last_log >= 1000) {
            last_log = now;
            spk_postproc_get_diag(&spk_diag);
            LOG_INF("SPK PLAY frames=%u errs=%u q=%u mode=%s aud=%u gap=%u sil=%u in_rms=%u out_rms=%u act_rms=%u idle_rms=%u lowcut=%upm gate=%upm comp=%upm clip=%upm dc=%d",
                    g_spk_play_frames, g_spk_play_errs,
                    (unsigned)spk_ring_count(),
                    clip_mode ? "clip" : (stream_mode ? "stream" : "fill"),
                    (unsigned)session_audio_frames,
                    (unsigned)session_gap_fill_total,
                    (unsigned)session_silence_fill_total,
                    (unsigned)spk_diag.in_rms,
                    (unsigned)spk_diag.out_rms,
                    (unsigned)spk_diag.active_rms,
                    (unsigned)spk_diag.idle_rms,
                    (unsigned)spk_diag.low_removed_pm,
                    (unsigned)spk_diag.gate_attn_pm,
                    (unsigned)spk_diag.comp_attn_pm,
                    (unsigned)spk_diag.clip_pm,
                    (int)spk_diag.dc_out);
        }

        if (g_spk_buf_full) {
            size_t used = spk_ring_count();
            if (used <= SPK_LOW_WATER_FRAMES && app_uplink_service_is_ready()) {
                const char low[] = "BUF_LOW";
                (void)app_uplink_publish(APP_DATA_PART_CTRL,
                                         APP_UPLINK_PRIO_HIGH,
                                         low,
                                         sizeof(low) - 1,
                                         (uint32_t)k_uptime_get());
                g_spk_buf_full = false;
            }
        }

        if (spk_is_eos_received() && spk_ring_count() == 0U && !spk_running) {
            const char done[] = "PLAY_DONE";
            g_spk_t_done_ms = k_uptime_get();
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
            clip_mode = false;
            stream_mode = false;
            have_last_pcm = false;
            gap_fill_frames = 0U;
            spk_ring_reset();
            LOG_INF("SPK TIMING sid=%u ctrl=%lld first=%lld start=%lld eos=%lld done=%lld wait=%lld play=%lld total=%lld",
                    (unsigned)g_spk_play_sid,
                    g_spk_t_ctrl_ms,
                    g_spk_t_first_audio_ms,
                    g_spk_t_start_ms,
                    g_spk_t_eos_ms,
                    g_spk_t_done_ms,
                    (long long)((g_spk_t_first_audio_ms > 0 && g_spk_t_start_ms > 0) ? (g_spk_t_start_ms - g_spk_t_first_audio_ms) : -1),
                    (long long)((g_spk_t_start_ms > 0 && g_spk_t_done_ms > 0) ? (g_spk_t_done_ms - g_spk_t_start_ms) : -1),
                    (long long)((g_spk_t_ctrl_ms > 0 && g_spk_t_done_ms > 0) ? (g_spk_t_done_ms - g_spk_t_ctrl_ms) : -1));
            app_rtc_playback_critical_exit();
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
#if defined(CONFIG_MIC_LEVEL_STEREO_TEST)
    int32_t peak_l = 0;
    int32_t peak_r = 0;
    int64_t sum_l = 0;
    int64_t sum_r = 0;
    uint64_t sum_sq_l = 0;
    uint64_t sum_sq_r = 0;
#else
    int32_t peak = 0;
    int64_t sum = 0;
    uint64_t sum_sq = 0;
#endif
    uint32_t sample_cnt = 0;
    int16_t first_samples[6] = {0};
    bool first_samples_valid = false;

    while (1) {
        void *buf = NULL;
        size_t len = 0;
        int r = hal_mic_read_block(&buf, &len, 1000);
        if (r == HAL_OK && buf != NULL && len > 0) {
            int16_t *pcm = (int16_t *)buf;
            size_t samples = len / sizeof(int16_t);
            if (!first_samples_valid) {
                for (size_t i = 0; i < ARRAY_SIZE(first_samples); i++) {
                    first_samples[i] = (i < samples) ? pcm[i] : 0;
                }
                first_samples_valid = true;
            }
#if defined(CONFIG_MIC_LEVEL_STEREO_TEST)
            for (size_t i = 0; i + 1U < samples; i += 2U) {
                int32_t vl = pcm[i];
                int32_t vr = pcm[i + 1U];
                int32_t avl = (vl < 0) ? -vl : vl;
                int32_t avr = (vr < 0) ? -vr : vr;
                if (avl > peak_l) {
                    peak_l = avl;
                }
                if (avr > peak_r) {
                    peak_r = avr;
                }
                sum_l += vl;
                sum_r += vr;
                sum_sq_l += (uint64_t)(vl * (int64_t)vl);
                sum_sq_r += (uint64_t)(vr * (int64_t)vr);
            }
            sample_cnt += (uint32_t)(samples / 2U);
#else
            for (size_t i = 0; i < samples; i++) {
                int32_t v = pcm[i];
                int32_t av = (v < 0) ? -v : v;
                if (av > peak) {
                    peak = av;
                }
                sum += v;
                sum_sq += (uint64_t)(v * (int64_t)v);
            }
            sample_cnt += (uint32_t)samples;
#endif
            frames++;
            (void)hal_mic_release(buf);
        }

        uint64_t now = k_uptime_get();
        if (now - last_log >= 1000) {
#if defined(CONFIG_MIC_LEVEL_STEREO_TEST)
            uint32_t rms_l = 0;
            uint32_t rms_r = 0;
            int32_t mean_l = 0;
            int32_t mean_r = 0;
            if (sample_cnt > 0) {
                float32_t out_l = 0.0f;
                float32_t out_r = 0.0f;
                arm_sqrt_f32((float32_t)sum_sq_l / (float32_t)sample_cnt, &out_l);
                arm_sqrt_f32((float32_t)sum_sq_r / (float32_t)sample_cnt, &out_r);
                rms_l = (uint32_t)out_l;
                rms_r = (uint32_t)out_r;
                mean_l = (int32_t)(sum_l / (int64_t)sample_cnt);
                mean_r = (int32_t)(sum_r / (int64_t)sample_cnt);
            }
            LOG_INF("MIC stereo: frames=%u samples/ch=%u L(peak=%d rms=%u mean=%d) R(peak=%d rms=%u mean=%d) first=[%d,%d,%d,%d,%d,%d]",
                    frames,
                    (unsigned int)sample_cnt,
                    (int)peak_l,
                    (unsigned int)rms_l,
                    (int)mean_l,
                    (int)peak_r,
                    (unsigned int)rms_r,
                    (int)mean_r,
                    (int)first_samples[0],
                    (int)first_samples[1],
                    (int)first_samples[2],
                    (int)first_samples[3],
                    (int)first_samples[4],
                    (int)first_samples[5]);
#else
            uint32_t rms = 0;
            int32_t mean = 0;
            if (sample_cnt > 0) {
                float32_t out = 0.0f;
                arm_sqrt_f32((float32_t)sum_sq / (float32_t)sample_cnt, &out);
                rms = (uint32_t)out;
                mean = (int32_t)(sum / (int64_t)sample_cnt);
            }
            LOG_INF("MIC mono: frames=%u samples=%u peak=%d rms=%u mean=%d first=[%d,%d,%d,%d,%d,%d]",
                    frames,
                    (unsigned int)sample_cnt,
                    (int)peak,
                    (unsigned int)rms,
                    (int)mean,
                    (int)first_samples[0],
                    (int)first_samples[1],
                    (int)first_samples[2],
                    (int)first_samples[3],
                    (int)first_samples[4],
                    (int)first_samples[5]);
#endif
            last_log = now;
#if defined(CONFIG_MIC_LEVEL_STEREO_TEST)
            peak_l = 0;
            peak_r = 0;
            sum_l = 0;
            sum_r = 0;
            sum_sq_l = 0;
            sum_sq_r = 0;
#else
            peak = 0;
            sum = 0;
            sum_sq = 0;
#endif
            sample_cnt = 0;
            frames = 0;
            first_samples_valid = false;
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
RT_THREAD_STACK_DEFINE(g_mic_test_stack, MIC_TEST_STACK_SIZE);

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
        payload_len = audio_encode_ima_adpcm_16k((const int16_t *)pcm,
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
        while (g_mic_pause_req) {
            k_msleep(20);
        }

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
            if (g_mic_pause_req) {
                break;
            }
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
                if (g_mic_pause_req || r == HAL_EBUSY) {
                    break;
                }
                LOG_ERR("hal_mic_read_block failed: %d", r);
                break;
            }
        }
        (void)hal_mic_stop();
        if (g_mic_pause_req) {
            mic_drop_queue();
            while (g_mic_pause_req) {
                k_msleep(20);
            }
        } else {
            k_msleep(200);
        }
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
        if (g_mic_pause_req) {
            mic_drop_queue();
            k_msleep(20);
            continue;
        }

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

        mic_dsp_process((int16_t *)msg.buf, msg.len / sizeof(int16_t));
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
    k_mutex_init(&g_spk_ring_lock);
    spk_ring_reset();

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
    } else {
        g_mic_cap_started = true;
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
    } else {
        g_mic_up_started = true;
    }
#endif

#ifdef CONFIG_SPK_STREAM
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

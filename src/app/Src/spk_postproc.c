#include <string.h>
#include <math.h>

#include <zephyr/sys/util.h>
#include <arm_math.h>

#include "spk_postproc.h"
#include "error.h"
#define OUTSIDE_SPEEX 1
#define RANDOM_PREFIX nrfspx
#define FLOATING_POINT 1
#define EXPORT
#include "speex_resampler.h"

#define SPK_PP_IN_8K_SAMPLES    80U
#define SPK_PP_OUT_16K_SAMPLES 160U
#define SPK_PP_FADE_SAMPLES     80U
#define SPK_PP_RESAMPLE_QUALITY 6U

#define SPK_PP_EXP_THRESHOLD 0.018f
#define SPK_PP_EXP_FLOOR     0.78f
#define SPK_PP_COMP_THRESH   0.74f
#define SPK_PP_COMP_RATIO    3.6f
#define SPK_PP_LIMIT         0.972f
#define SPK_PP_HP_R          0.986f
#define SPK_PP_GATE_TH       0.0006f
#define SPK_PP_GATE_FLOOR    0.985f
#define SPK_PP_VOLUME_DEFAULT 500U
#define SPK_PP_VOLUME_MAX_GAIN 1.55f
#define SPK_PP_FINAL_BOOST   1.10f

static const float32_t g_spk_pp_notch_coeffs[10] = {
    1.0f, -1.99961448f, 1.0f, 1.98961641f, -0.99002500f,
    1.0f, -1.99845807f, 1.0f, 1.98846578f, -0.99002500f,
};

static const float32_t g_spk_pp_8k_coeffs[20] = {
    0.951238588f, -1.902477175f, 0.951238588f, 1.900098769f, -0.904855581f,
    0.638917193f, 1.277834387f, 0.638917193f, -1.142929821f, -0.412738952f,
    1.052644880f, -0.936140546f, 0.388795353f, 0.936140546f, -0.441440233f,
    0.911177008f, 0.818042061f, 0.480559273f, -0.818042061f, -0.391736281f,
};

static const float32_t g_spk_pp_16k_coeffs[10] = {
    0.248325656f, 0.496651312f, 0.248325656f, 0.184202363f, -0.177504988f,
    0.929789669f, -0.504092981f, 0.387468771f, 0.504092981f, -0.317258440f,
};

static const float32_t g_spk_pp_16k_lpf_coeffs[5] = {
    0.248325656f, 0.496651312f, 0.248325656f, 0.184202363f, -0.177504988f,
};

static arm_biquad_casd_df1_inst_f32 g_spk_pp_8k_eq;
static arm_biquad_casd_df1_inst_f32 g_spk_pp_16k_eq;
static arm_biquad_casd_df1_inst_f32 g_spk_pp_notch;
static arm_biquad_casd_df1_inst_f32 g_spk_pp_16k_lpf;
static float32_t g_spk_pp_8k_state[16];
static float32_t g_spk_pp_16k_state[8];
static float32_t g_spk_pp_notch_state[8];
static float32_t g_spk_pp_16k_lpf_state[4];
static float32_t g_spk_pp_in_f32[SPK_PP_OUT_16K_SAMPLES];
static float32_t g_spk_pp_mid_f32[SPK_PP_OUT_16K_SAMPLES];
static float32_t g_spk_pp_out_f32[SPK_PP_OUT_16K_SAMPLES];
static int16_t g_spk_pp_tmp8_i16[SPK_PP_IN_8K_SAMPLES];
static int16_t g_spk_pp_tmp16_i16[SPK_PP_OUT_16K_SAMPLES];
static SpeexResamplerState *g_spk_pp_resampler;
static bool g_spk_pp_ready;
static uint16_t g_spk_pp_volume_percent = SPK_PP_VOLUME_DEFAULT;
static float32_t g_spk_pp_exp_env;
static float32_t g_spk_pp_comp_env;
static uint32_t g_spk_pp_fade_in_left;
static float32_t g_spk_pp_hp_x1;
static float32_t g_spk_pp_hp_y1;
static float32_t g_spk_pp_gate_env;
static struct {
    uint32_t frames;
    uint32_t samples;
    uint32_t active_samples;
    uint32_t idle_samples;
    uint32_t clip_samples;
    uint64_t in_energy_q30;
    uint64_t out_energy_q30;
    uint64_t active_energy_q30;
    uint64_t idle_energy_q30;
    uint64_t low_removed_energy_q30;
    uint64_t gate_attn_acc_pm;
    uint64_t comp_attn_acc_pm;
    int64_t dc_out_acc_q15;
} g_spk_pp_diag;

#define SPK_PP_DIAG_ACTIVE_TH 0.040f

static inline float32_t spk_pp_absf(float32_t x)
{
    return x >= 0.0f ? x : -x;
}

static float32_t spk_pp_output_gain(void)
{
    float32_t norm = (float32_t)g_spk_pp_volume_percent / 100.0f;
    if (norm <= 0.0f) {
        return 0.0f;
    }
    if (norm > 5.0f) {
        norm = 5.0f;
    }
    return SPK_PP_VOLUME_MAX_GAIN * sqrtf(norm);
}

static void spk_pp_process_8k_cleanup(float32_t *pcm, size_t samples)
{
    for (size_t i = 0; i < samples; i++) {
        float32_t x = pcm[i];
        float32_t ax = spk_pp_absf(x);
        float32_t coeff = (ax > g_spk_pp_exp_env) ? 0.22f : 0.025f;
        g_spk_pp_exp_env += coeff * (ax - g_spk_pp_exp_env);

        float32_t gain = 1.0f;
        if (g_spk_pp_exp_env < SPK_PP_EXP_THRESHOLD) {
            float32_t norm = g_spk_pp_exp_env / SPK_PP_EXP_THRESHOLD;
            gain = SPK_PP_EXP_FLOOR + (1.0f - SPK_PP_EXP_FLOOR) * norm * norm;
        }
        pcm[i] = x * gain;
    }
}

static void spk_pp_process_16k_output(float32_t *pcm, size_t samples, bool fade_out_tail)
{
    size_t fade_samples = fade_out_tail ? MIN(samples, (size_t)SPK_PP_FADE_SAMPLES) : 0U;
    size_t fade_start = samples - fade_samples;

    for (size_t i = 0; i < samples; i++) {
        float32_t in = pcm[i];
        float32_t hp = in - g_spk_pp_hp_x1 + SPK_PP_HP_R * g_spk_pp_hp_y1;
        g_spk_pp_hp_x1 = in;
        g_spk_pp_hp_y1 = hp;
        float32_t low = in - hp;

        float32_t y = hp * spk_pp_output_gain();
        float32_t gate_gain = 1.0f;
        float32_t comp_gain = 1.0f;
        float32_t ay = spk_pp_absf(y);
        float32_t gate_coeff = (ay > g_spk_pp_gate_env) ? 0.06f : 0.002f;
        g_spk_pp_gate_env += gate_coeff * (ay - g_spk_pp_gate_env);
        if (g_spk_pp_gate_env < SPK_PP_GATE_TH) {
            float32_t norm = g_spk_pp_gate_env / SPK_PP_GATE_TH;
            gate_gain = SPK_PP_GATE_FLOOR + (1.0f - SPK_PP_GATE_FLOOR) * norm * norm;
            y *= gate_gain;
            ay = spk_pp_absf(y);
        }

        float32_t coeff = (ay > g_spk_pp_comp_env) ? 0.08f : 0.004f;
        g_spk_pp_comp_env += coeff * (ay - g_spk_pp_comp_env);

        if (g_spk_pp_comp_env > SPK_PP_COMP_THRESH) {
            float32_t over = g_spk_pp_comp_env - SPK_PP_COMP_THRESH;
            float32_t compressed = SPK_PP_COMP_THRESH + over / SPK_PP_COMP_RATIO;
            comp_gain = compressed / (g_spk_pp_comp_env + 1.0e-6f);
            y *= comp_gain;
            ay = spk_pp_absf(y);
        }

        if (g_spk_pp_fade_in_left > 0U) {
            uint32_t done = SPK_PP_FADE_SAMPLES - g_spk_pp_fade_in_left;
            float32_t fade = (float32_t)(done + 1U) / (float32_t)SPK_PP_FADE_SAMPLES;
            y *= fade;
            g_spk_pp_fade_in_left--;
            ay = spk_pp_absf(y);
        }

        if (fade_out_tail && i >= fade_start) {
            size_t rem = samples - i;
            float32_t fade = (float32_t)rem / (float32_t)(fade_samples + 1U);
            y *= fade;
            ay = spk_pp_absf(y);
        }

        bool clipped = false;
        y *= SPK_PP_FINAL_BOOST;
        ay = spk_pp_absf(y);
        if (ay > SPK_PP_LIMIT) {
            float32_t sign = (y >= 0.0f) ? 1.0f : -1.0f;
            float32_t over = ay - SPK_PP_LIMIT;
            ay = SPK_PP_LIMIT + over * 0.10f;
            if (ay > 0.985f) {
                ay = 0.985f;
            }
            y = sign * ay;
            clipped = true;
        }

        float32_t abs_in = spk_pp_absf(in);
        if (abs_in >= SPK_PP_DIAG_ACTIVE_TH) {
            g_spk_pp_diag.active_samples++;
            g_spk_pp_diag.active_energy_q30 += (uint64_t)(in * in * 1073741824.0f);
        } else {
            g_spk_pp_diag.idle_samples++;
            g_spk_pp_diag.idle_energy_q30 += (uint64_t)(y * y * 1073741824.0f);
        }
        g_spk_pp_diag.samples++;
        g_spk_pp_diag.in_energy_q30 += (uint64_t)(in * in * 1073741824.0f);
        g_spk_pp_diag.out_energy_q30 += (uint64_t)(y * y * 1073741824.0f);
        g_spk_pp_diag.low_removed_energy_q30 += (uint64_t)(low * low * 1073741824.0f);
        g_spk_pp_diag.gate_attn_acc_pm += (uint64_t)(gate_gain * 1000.0f);
        g_spk_pp_diag.comp_attn_acc_pm += (uint64_t)(comp_gain * 1000.0f);
        g_spk_pp_diag.dc_out_acc_q15 += (int32_t)(y * 32768.0f);
        if (clipped) {
            g_spk_pp_diag.clip_samples++;
        }
        pcm[i] = y;
    }
}

int spk_postproc_init(void)
{
    if (g_spk_pp_ready) {
        return HAL_OK;
    }

    arm_biquad_cascade_df1_init_f32(&g_spk_pp_8k_eq, 4, (float32_t *)g_spk_pp_8k_coeffs, g_spk_pp_8k_state);
    arm_biquad_cascade_df1_init_f32(&g_spk_pp_16k_eq, 2, (float32_t *)g_spk_pp_16k_coeffs, g_spk_pp_16k_state);
    arm_biquad_cascade_df1_init_f32(&g_spk_pp_notch, 2, (float32_t *)g_spk_pp_notch_coeffs, g_spk_pp_notch_state);
    arm_biquad_cascade_df1_init_f32(&g_spk_pp_16k_lpf, 1, (float32_t *)g_spk_pp_16k_lpf_coeffs, g_spk_pp_16k_lpf_state);

    int err = 0;
    g_spk_pp_resampler = speex_resampler_init(1,
                                              8000U,
                                              16000U,
                                              SPK_PP_RESAMPLE_QUALITY,
                                              &err);
    if (g_spk_pp_resampler == NULL || err != RESAMPLER_ERR_SUCCESS) {
        g_spk_pp_resampler = NULL;
        return HAL_ENOMEM;
    }

    speex_resampler_skip_zeros(g_spk_pp_resampler);
    g_spk_pp_ready = true;
    spk_postproc_reset();
    return HAL_OK;
}

void spk_postproc_reset(void)
{
    memset(g_spk_pp_8k_state, 0, sizeof(g_spk_pp_8k_state));
    memset(g_spk_pp_16k_state, 0, sizeof(g_spk_pp_16k_state));
    memset(g_spk_pp_notch_state, 0, sizeof(g_spk_pp_notch_state));
    memset(g_spk_pp_16k_lpf_state, 0, sizeof(g_spk_pp_16k_lpf_state));
    g_spk_pp_exp_env = 0.0f;
    g_spk_pp_comp_env = 0.0f;
    g_spk_pp_fade_in_left = SPK_PP_FADE_SAMPLES;
    g_spk_pp_hp_x1 = 0.0f;
    g_spk_pp_hp_y1 = 0.0f;
    g_spk_pp_gate_env = 0.0f;
    memset(&g_spk_pp_diag, 0, sizeof(g_spk_pp_diag));

    if (g_spk_pp_resampler != NULL) {
        speex_resampler_reset_mem(g_spk_pp_resampler);
        speex_resampler_skip_zeros(g_spk_pp_resampler);
    }
}

void spk_postproc_set_volume_percent(uint16_t percent)
{
    if (percent > 500U) {
        percent = 500U;
    }
    g_spk_pp_volume_percent = percent;
}

uint16_t spk_postproc_get_volume_percent(void)
{
    return g_spk_pp_volume_percent;
}

int spk_postproc_process_frame(const int16_t *in_pcm,
                               size_t in_samples,
                               uint32_t in_rate_hz,
                               int16_t *out_pcm,
                               size_t out_cap_samples,
                               size_t *out_samples,
                               bool fade_out_tail)
{
    if (in_pcm == NULL || out_pcm == NULL || out_samples == NULL || out_cap_samples < SPK_PP_OUT_16K_SAMPLES) {
        return HAL_EINVAL;
    }
    if (!g_spk_pp_ready) {
        int ret = spk_postproc_init();
        if (ret != HAL_OK) {
            return ret;
        }
    }

    size_t work_samples = 0U;
    if (in_rate_hz == 8000U) {
        if (in_samples == 0U || in_samples > SPK_PP_IN_8K_SAMPLES) {
            return HAL_EINVAL;
        }

        for (size_t i = 0; i < in_samples; i++) {
            g_spk_pp_in_f32[i] = (float32_t)in_pcm[i] / 32768.0f;
        }

        arm_biquad_cascade_df1_f32(&g_spk_pp_8k_eq, g_spk_pp_in_f32, g_spk_pp_mid_f32, (uint32_t)in_samples);
        spk_pp_process_8k_cleanup(g_spk_pp_mid_f32, in_samples);

        for (size_t i = 0; i < in_samples; i++) {
            float32_t v = g_spk_pp_mid_f32[i];
            if (v > 0.999f) {
                v = 0.999f;
            } else if (v < -0.999f) {
                v = -0.999f;
            }
            g_spk_pp_tmp8_i16[i] = (int16_t)(v * 32767.0f);
        }

        spx_uint32_t in_len = (spx_uint32_t)in_samples;
        spx_uint32_t out_len = (spx_uint32_t)out_cap_samples;
        int err = speex_resampler_process_int(g_spk_pp_resampler,
                                              0,
                                              g_spk_pp_tmp8_i16,
                                              &in_len,
                                              g_spk_pp_tmp16_i16,
                                              &out_len);
        if (err != RESAMPLER_ERR_SUCCESS || out_len == 0U) {
            return HAL_EIO;
        }

        work_samples = (size_t)out_len;
        if (work_samples < SPK_PP_OUT_16K_SAMPLES) {
            int16_t tail = g_spk_pp_tmp16_i16[work_samples - 1U];
            for (size_t i = work_samples; i < SPK_PP_OUT_16K_SAMPLES; i++) {
                g_spk_pp_tmp16_i16[i] = tail;
            }
            work_samples = SPK_PP_OUT_16K_SAMPLES;
        } else if (work_samples > SPK_PP_OUT_16K_SAMPLES) {
            work_samples = SPK_PP_OUT_16K_SAMPLES;
        }

        for (size_t i = 0; i < work_samples; i++) {
            g_spk_pp_in_f32[i] = (float32_t)g_spk_pp_tmp16_i16[i] / 32768.0f;
        }
    } else if (in_rate_hz == 16000U) {
        if (in_samples == 0U || in_samples > out_cap_samples) {
            return HAL_EINVAL;
        }
        work_samples = in_samples;
        for (size_t i = 0; i < work_samples; i++) {
            g_spk_pp_in_f32[i] = (float32_t)in_pcm[i] / 32768.0f;
        }
    } else {
        return HAL_EINVAL;
    }

    arm_biquad_cascade_df1_f32(&g_spk_pp_notch, g_spk_pp_in_f32, g_spk_pp_mid_f32, (uint32_t)work_samples);
    arm_biquad_cascade_df1_f32(&g_spk_pp_16k_lpf, g_spk_pp_mid_f32, g_spk_pp_out_f32, (uint32_t)work_samples);
    spk_pp_process_16k_output(g_spk_pp_out_f32, work_samples, fade_out_tail);
    g_spk_pp_diag.frames++;

    for (size_t i = 0; i < work_samples; i++) {
        float32_t y = g_spk_pp_out_f32[i];
        if (y > 0.999f) {
            y = 0.999f;
        } else if (y < -0.999f) {
            y = -0.999f;
        }
        out_pcm[i] = (int16_t)(y * 32767.0f);
    }

    *out_samples = work_samples;
    return HAL_OK;
}

void spk_postproc_get_diag(spk_postproc_diag_t *out)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    out->frames = g_spk_pp_diag.frames;
    out->samples = g_spk_pp_diag.samples;
    if (g_spk_pp_diag.samples == 0U) {
        return;
    }

    float in_rms = sqrtf((float)g_spk_pp_diag.in_energy_q30 /
                         ((float)g_spk_pp_diag.samples * 1073741824.0f));
    float out_rms = sqrtf((float)g_spk_pp_diag.out_energy_q30 /
                          ((float)g_spk_pp_diag.samples * 1073741824.0f));
    float active_rms = 0.0f;
    float idle_rms = 0.0f;
    if (g_spk_pp_diag.active_samples > 0U) {
        active_rms = sqrtf((float)g_spk_pp_diag.active_energy_q30 /
                           ((float)g_spk_pp_diag.active_samples * 1073741824.0f));
    }
    if (g_spk_pp_diag.idle_samples > 0U) {
        idle_rms = sqrtf((float)g_spk_pp_diag.idle_energy_q30 /
                         ((float)g_spk_pp_diag.idle_samples * 1073741824.0f));
    }

    out->in_rms = (uint16_t)(in_rms * 32767.0f);
    out->out_rms = (uint16_t)(out_rms * 32767.0f);
    out->active_rms = (uint16_t)(active_rms * 32767.0f);
    out->idle_rms = (uint16_t)(idle_rms * 32767.0f);
    out->low_removed_pm = (uint16_t)((g_spk_pp_diag.low_removed_energy_q30 * 1000ULL) /
                                     (g_spk_pp_diag.in_energy_q30 + 1ULL));
    out->gate_attn_pm = 1000U - (uint16_t)(g_spk_pp_diag.gate_attn_acc_pm / g_spk_pp_diag.samples);
    out->comp_attn_pm = 1000U - (uint16_t)(g_spk_pp_diag.comp_attn_acc_pm / g_spk_pp_diag.samples);
    out->clip_pm = (uint16_t)((g_spk_pp_diag.clip_samples * 1000ULL) / g_spk_pp_diag.samples);
    out->dc_out = (int16_t)(g_spk_pp_diag.dc_out_acc_q15 / (int64_t)g_spk_pp_diag.samples);
}

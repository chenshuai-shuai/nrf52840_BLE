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
#define SPK_PP_RESAMPLE_QUALITY 4U

#define SPK_PP_EXP_THRESHOLD 0.018f
#define SPK_PP_EXP_FLOOR     0.30f
#define SPK_PP_OUT_GAIN      0.46f
#define SPK_PP_COMP_THRESH   0.42f
#define SPK_PP_COMP_RATIO    2.1f
#define SPK_PP_LIMIT         0.80f

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

static arm_biquad_casd_df1_inst_f32 g_spk_pp_8k_eq;
static arm_biquad_casd_df1_inst_f32 g_spk_pp_16k_eq;
static float32_t g_spk_pp_8k_state[16];
static float32_t g_spk_pp_16k_state[8];
static float32_t g_spk_pp_in_f32[SPK_PP_OUT_16K_SAMPLES];
static float32_t g_spk_pp_mid_f32[SPK_PP_OUT_16K_SAMPLES];
static float32_t g_spk_pp_out_f32[SPK_PP_OUT_16K_SAMPLES];
static int16_t g_spk_pp_tmp8_i16[SPK_PP_IN_8K_SAMPLES];
static int16_t g_spk_pp_tmp16_i16[SPK_PP_OUT_16K_SAMPLES];
static SpeexResamplerState *g_spk_pp_resampler;
static bool g_spk_pp_ready;
static float32_t g_spk_pp_exp_env;
static float32_t g_spk_pp_comp_env;
static uint32_t g_spk_pp_fade_in_left;

static inline float32_t spk_pp_absf(float32_t x)
{
    return x >= 0.0f ? x : -x;
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
        float32_t y = pcm[i] * SPK_PP_OUT_GAIN;
        float32_t ay = spk_pp_absf(y);
        float32_t coeff = (ay > g_spk_pp_comp_env) ? 0.18f : 0.008f;
        g_spk_pp_comp_env += coeff * (ay - g_spk_pp_comp_env);

        if (g_spk_pp_comp_env > SPK_PP_COMP_THRESH) {
            float32_t over = g_spk_pp_comp_env - SPK_PP_COMP_THRESH;
            float32_t compressed = SPK_PP_COMP_THRESH + over / SPK_PP_COMP_RATIO;
            float32_t gain = compressed / (g_spk_pp_comp_env + 1.0e-6f);
            y *= gain;
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

        if (ay > SPK_PP_LIMIT) {
            float32_t over = ay - SPK_PP_LIMIT;
            ay = SPK_PP_LIMIT + over * 0.16f;
            if (ay > 0.92f) {
                ay = 0.92f;
            }
            y = (y >= 0.0f) ? ay : -ay;
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
    g_spk_pp_exp_env = 0.0f;
    g_spk_pp_comp_env = 0.0f;
    g_spk_pp_fade_in_left = SPK_PP_FADE_SAMPLES;

    if (g_spk_pp_resampler != NULL) {
        speex_resampler_reset_mem(g_spk_pp_resampler);
        speex_resampler_skip_zeros(g_spk_pp_resampler);
    }
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

    arm_biquad_cascade_df1_f32(&g_spk_pp_16k_eq, g_spk_pp_in_f32, g_spk_pp_out_f32, (uint32_t)work_samples);
    spk_pp_process_16k_output(g_spk_pp_out_f32, work_samples, fade_out_tail);

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

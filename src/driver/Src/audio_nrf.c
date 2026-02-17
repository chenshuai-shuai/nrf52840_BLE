#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/audio/dmic.h>

#include "hal_audio.h"
#include "error.h"

#define PDM_NODE DT_NODELABEL(pdm0)
#define I2S_NODE DT_NODELABEL(i2s0)

#define AUDIO_NRF_DEFAULT_SAMPLE_RATE 16000U
#define AUDIO_NRF_DEFAULT_BITS 16U
#define AUDIO_NRF_DEFAULT_CHANNELS 1U

#define AUDIO_NRF_MAX_SAMPLE_RATE 48000U
#define AUDIO_NRF_MAX_BITS 16U
#define AUDIO_NRF_MAX_CHANNELS 2U
#define AUDIO_NRF_MAX_FRAME_SAMPLES 480U
#define AUDIO_NRF_BLOCK_COUNT 6U

#define AUDIO_NRF_MAX_BLOCK_SIZE \
    (AUDIO_NRF_MAX_FRAME_SAMPLES * AUDIO_NRF_MAX_CHANNELS * (AUDIO_NRF_MAX_BITS / 8U))

K_MEM_SLAB_DEFINE_STATIC(audio_mem_slab, AUDIO_NRF_MAX_BLOCK_SIZE, AUDIO_NRF_BLOCK_COUNT, 4);

struct audio_nrf_state {
    const struct device *pdm_dev;
    const struct device *i2s_dev;
    struct dmic_cfg dmic_cfg;
    struct pcm_stream_cfg stream_cfg;
    bool pdm_active;
};

static struct audio_nrf_state g_audio;

static int audio_nrf_init(void)
{
    if (DT_NODE_HAS_STATUS(PDM_NODE, okay)) {
        g_audio.pdm_dev = device_get_binding(DEVICE_DT_NAME(PDM_NODE));
        if (g_audio.pdm_dev == NULL || !device_is_ready(g_audio.pdm_dev)) {
            return HAL_ENODEV;
        }
    }

    if (DT_NODE_HAS_STATUS(I2S_NODE, okay)) {
        g_audio.i2s_dev = device_get_binding(DEVICE_DT_NAME(I2S_NODE));
        if (g_audio.i2s_dev == NULL || !device_is_ready(g_audio.i2s_dev)) {
            return HAL_ENODEV;
        }
    }

    return HAL_OK;
}

static int audio_nrf_configure_pdm(const hal_audio_cfg_t *cfg)
{
    uint32_t sample_rate = AUDIO_NRF_DEFAULT_SAMPLE_RATE;
    uint8_t bits = AUDIO_NRF_DEFAULT_BITS;
    uint8_t channels = AUDIO_NRF_DEFAULT_CHANNELS;
    uint16_t frame_samples = 0U;

    if (cfg != NULL) {
        if (cfg->format.sample_rate > 0U) {
            sample_rate = cfg->format.sample_rate;
        }
        if (cfg->format.bits_per_sample > 0U) {
            bits = cfg->format.bits_per_sample;
        }
        if (cfg->format.channels > 0U) {
            channels = cfg->format.channels;
        }
        frame_samples = cfg->frame_samples;
    }

    if (sample_rate == 0U || sample_rate > AUDIO_NRF_MAX_SAMPLE_RATE) {
        return HAL_EINVAL;
    }
    if (bits != 16U) {
        return HAL_ENOTSUP;
    }
    if (channels == 0U || channels > AUDIO_NRF_MAX_CHANNELS) {
        return HAL_EINVAL;
    }

    if (frame_samples == 0U) {
        frame_samples = (uint16_t)(sample_rate / 1000U);
        if (frame_samples == 0U) {
            frame_samples = 16U;
        }
    }
    if (frame_samples > AUDIO_NRF_MAX_FRAME_SAMPLES) {
        return HAL_EINVAL;
    }

    uint32_t block_size = frame_samples * channels * (bits / 8U);
    if (block_size == 0U || block_size > AUDIO_NRF_MAX_BLOCK_SIZE) {
        return HAL_EINVAL;
    }

    g_audio.stream_cfg.pcm_width = bits;
    g_audio.stream_cfg.pcm_rate = sample_rate;
    g_audio.stream_cfg.block_size = block_size;
    g_audio.stream_cfg.mem_slab = &audio_mem_slab;

    g_audio.dmic_cfg.io.min_pdm_clk_freq = 1000000U;
    g_audio.dmic_cfg.io.max_pdm_clk_freq = 3500000U;
    g_audio.dmic_cfg.io.min_pdm_clk_dc = 40U;
    g_audio.dmic_cfg.io.max_pdm_clk_dc = 60U;
    g_audio.dmic_cfg.io.pdm_clk_pol = 0U;
    g_audio.dmic_cfg.io.pdm_data_pol = 0U;
    g_audio.dmic_cfg.io.pdm_clk_skew = 0U;

    g_audio.dmic_cfg.streams = &g_audio.stream_cfg;

    g_audio.dmic_cfg.channel.req_num_streams = 1U;
    g_audio.dmic_cfg.channel.req_num_chan = channels;
    if (channels == 1U) {
        g_audio.dmic_cfg.channel.req_chan_map_lo =
            dmic_build_channel_map(0U, 0U, PDM_CHAN_LEFT);
    } else {
        g_audio.dmic_cfg.channel.req_chan_map_lo =
            dmic_build_channel_map(0U, 0U, PDM_CHAN_LEFT) |
            dmic_build_channel_map(1U, 0U, PDM_CHAN_RIGHT);
    }

    return HAL_OK;
}

static int audio_nrf_start(hal_audio_dir_t dir, const hal_audio_cfg_t *cfg)
{
    if (dir != HAL_AUDIO_DIR_INPUT) {
        return HAL_ENOTSUP;
    }
    if (g_audio.pdm_dev == NULL) {
        return HAL_ENODEV;
    }

    int ret = audio_nrf_configure_pdm(cfg);
    if (ret != HAL_OK) {
        return ret;
    }

    ret = dmic_configure(g_audio.pdm_dev, &g_audio.dmic_cfg);
    if (ret < 0) {
        return ret;
    }

    ret = dmic_trigger(g_audio.pdm_dev, DMIC_TRIGGER_START);
    if (ret < 0) {
        return ret;
    }

    g_audio.pdm_active = true;
    return HAL_OK;
}

static int audio_nrf_stop(hal_audio_dir_t dir)
{
    if (dir != HAL_AUDIO_DIR_INPUT) {
        return HAL_ENOTSUP;
    }
    if (!g_audio.pdm_active || g_audio.pdm_dev == NULL) {
        return HAL_EBUSY;
    }

    int ret = dmic_trigger(g_audio.pdm_dev, DMIC_TRIGGER_STOP);
    if (ret < 0) {
        return ret;
    }

    g_audio.pdm_active = false;
    return HAL_OK;
}

static int audio_nrf_read_block(void **buf, size_t *len, int timeout_ms)
{
    if (!g_audio.pdm_active || g_audio.pdm_dev == NULL) {
        return HAL_EBUSY;
    }
    if (buf == NULL || len == NULL) {
        return HAL_EINVAL;
    }

    int32_t timeout = timeout_ms < 0 ? SYS_FOREVER_MS : (int32_t)timeout_ms;
    int ret = dmic_read(g_audio.pdm_dev, 0U, buf, len, timeout);
    if (ret < 0) {
        return ret;
    }

    return HAL_OK;
}

static int audio_nrf_release(void *buf)
{
    if (buf == NULL) {
        return HAL_EINVAL;
    }

    k_mem_slab_free(&audio_mem_slab, buf);
    return HAL_OK;
}

static int audio_nrf_read(void *buf, size_t len, int timeout_ms)
{
    ARG_UNUSED(buf);
    ARG_UNUSED(len);
    ARG_UNUSED(timeout_ms);

    return HAL_ENOTSUP;
}

static int audio_nrf_write(const void *buf, size_t len, int timeout_ms)
{
    ARG_UNUSED(buf);
    ARG_UNUSED(len);
    ARG_UNUSED(timeout_ms);

    return HAL_ENOTSUP;
}

static const hal_audio_ops_t g_audio_ops = {
    .init = audio_nrf_init,
    .start = audio_nrf_start,
    .stop = audio_nrf_stop,
    .read_block = audio_nrf_read_block,
    .release = audio_nrf_release,
    .read = audio_nrf_read,
    .write = audio_nrf_write,
};

int audio_nrf_register(void)
{
    return hal_audio_register(&g_audio_ops);
}

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <string.h>

#include "hal_mic.h"
#include "error.h"

LOG_MODULE_REGISTER(mic_nrf, LOG_LEVEL_INF);

#define MIC_NODE DT_NODELABEL(pdm0)

#define MIC_SAMPLE_RATE_HZ   16000U
#define MIC_BITS_PER_SAMPLE  16U
#define MIC_CHANNELS         1U
#define MIC_FRAME_SAMPLES    160U /* 10ms @ 16kHz */

#define MIC_MAX_BLOCK_SIZE   512U
#define MIC_BLOCK_COUNT      32U

K_MEM_SLAB_DEFINE_STATIC(mic_mem_slab, MIC_MAX_BLOCK_SIZE, MIC_BLOCK_COUNT, 4);

struct mic_state {
    const struct device *dev;
    struct dmic_cfg dmic_cfg;
    struct pcm_stream_cfg stream_cfg;
    size_t block_size;
    bool started;
};

static struct mic_state g_mic;

static int mic_nrf_init(void)
{
    if (!DT_NODE_HAS_STATUS(MIC_NODE, okay)) {
        return HAL_ENODEV;
    }

    g_mic.dev = DEVICE_DT_GET(MIC_NODE);
    if (g_mic.dev == NULL || !device_is_ready(g_mic.dev)) {
        LOG_ERR("DMIC device not ready");
        return HAL_ENODEV;
    }

    g_mic.block_size = MIC_FRAME_SAMPLES * MIC_CHANNELS * (MIC_BITS_PER_SAMPLE / 8U);
    if (g_mic.block_size == 0U || g_mic.block_size > MIC_MAX_BLOCK_SIZE ||
        (g_mic.block_size % 4U) != 0U) {
        LOG_ERR("invalid block_size=%u", (unsigned)g_mic.block_size);
        return HAL_EINVAL;
    }

    memset(&g_mic.stream_cfg, 0, sizeof(g_mic.stream_cfg));
    g_mic.stream_cfg.pcm_width = MIC_BITS_PER_SAMPLE;
    g_mic.stream_cfg.pcm_rate = MIC_SAMPLE_RATE_HZ;
    g_mic.stream_cfg.block_size = g_mic.block_size;
    g_mic.stream_cfg.mem_slab = &mic_mem_slab;

    memset(&g_mic.dmic_cfg, 0, sizeof(g_mic.dmic_cfg));
    g_mic.dmic_cfg.io.min_pdm_clk_freq = 1000000U;
    g_mic.dmic_cfg.io.max_pdm_clk_freq = 3200000U;
    g_mic.dmic_cfg.io.min_pdm_clk_dc = 40U;
    g_mic.dmic_cfg.io.max_pdm_clk_dc = 60U;

    g_mic.dmic_cfg.streams = &g_mic.stream_cfg;
    g_mic.dmic_cfg.channel.req_num_streams = 1U;
    g_mic.dmic_cfg.channel.req_num_chan = 1U;
    g_mic.dmic_cfg.channel.req_chan_map_lo =
        dmic_build_channel_map(0U, 0U, PDM_CHAN_RIGHT);
    g_mic.dmic_cfg.channel.req_chan_map_hi = 0U;

    (void)dmic_trigger(g_mic.dev, DMIC_TRIGGER_STOP);

    int ret = dmic_configure(g_mic.dev, &g_mic.dmic_cfg);
    if (ret) {
        LOG_ERR("dmic_configure failed: %d", ret);
        return ret;
    }

    LOG_INF("mic configured: block_size=%u", (unsigned)g_mic.block_size);
    return HAL_OK;
}

static int mic_nrf_start(void)
{
    if (g_mic.dev == NULL) {
        return HAL_ENODEV;
    }

    const struct device *clk = DEVICE_DT_GET(DT_NODELABEL(clock));
    if (device_is_ready(clk)) {
        (void)clock_control_on(clk, CLOCK_CONTROL_NRF_SUBSYS_HF);
        for (int i = 0; i < 50; i++) {
            if (clock_control_get_status(clk, CLOCK_CONTROL_NRF_SUBSYS_HF) ==
                CLOCK_CONTROL_STATUS_ON) {
                break;
            }
            k_msleep(1);
        }
    }

    int ret = dmic_trigger(g_mic.dev, DMIC_TRIGGER_START);
    if (ret) {
        LOG_ERR("dmic start failed: %d", ret);
        return ret;
    }

    g_mic.started = true;
    return HAL_OK;
}

static int mic_nrf_stop(void)
{
    if (g_mic.dev == NULL) {
        return HAL_ENODEV;
    }
    g_mic.started = false;
    return dmic_trigger(g_mic.dev, DMIC_TRIGGER_STOP);
}

static int mic_nrf_read_block(void **buf, size_t *len, int timeout_ms)
{
    if (!g_mic.started || g_mic.dev == NULL) {
        return HAL_EBUSY;
    }
    if (buf == NULL || len == NULL) {
        return HAL_EINVAL;
    }

    int32_t timeout = timeout_ms < 0 ? SYS_FOREVER_MS : (int32_t)timeout_ms;
    int ret = dmic_read(g_mic.dev, 0U, buf, len, timeout);
    if (ret) {
        return ret;
    }
    return HAL_OK;
}

static int mic_nrf_release(void *buf)
{
    if (buf == NULL) {
        return HAL_EINVAL;
    }
    k_mem_slab_free(&mic_mem_slab, buf);
    return HAL_OK;
}

static const hal_mic_ops_t g_mic_ops = {
    .init = mic_nrf_init,
    .start = mic_nrf_start,
    .stop = mic_nrf_stop,
    .read_block = mic_nrf_read_block,
    .release = mic_nrf_release,
};

int mic_nrf_register(void)
{
    return hal_mic_register(&g_mic_ops);
}

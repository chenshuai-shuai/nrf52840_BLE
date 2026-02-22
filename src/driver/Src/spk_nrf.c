#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <stdint.h>
#include <string.h>

#include "hal_spk.h"
#include "error.h"

LOG_MODULE_REGISTER(spk_nrf, LOG_LEVEL_INF);

#define I2S_DEV_NODE DT_NODELABEL(i2s0)

#if !DT_NODE_HAS_STATUS(I2S_DEV_NODE, okay)
#error "i2s0 is not enabled in devicetree"
#endif

#define AMP_SD_MODE_NODE DT_ALIAS(amp_sd_mode)
#define AMP_PWR_EN_NODE  DT_ALIAS(amp_pwr_en)

#if DT_NODE_HAS_STATUS(AMP_SD_MODE_NODE, okay)
#define AMP_HAS_SD_MODE 1
static const struct gpio_dt_spec amp_sd_mode = GPIO_DT_SPEC_GET(AMP_SD_MODE_NODE, gpios);
#else
#define AMP_HAS_SD_MODE 0
#endif

#if DT_NODE_HAS_STATUS(AMP_PWR_EN_NODE, okay)
#define AMP_HAS_PWR_EN 1
static const struct gpio_dt_spec amp_pwr_en = GPIO_DT_SPEC_GET(AMP_PWR_EN_NODE, gpios);
#else
#define AMP_HAS_PWR_EN 0
#endif

static const struct device *const i2s_dev = DEVICE_DT_GET(I2S_DEV_NODE);

#define SPK_SAMPLE_RATE_HZ        16000U
#define SPK_BITS_PER_SAMPLE       16U
#define SPK_NUM_CHANNELS          2U
#define SPK_BLOCK_SAMPLES_PER_CH  160U /* 10 ms @ 16 kHz */

#define SPK_BLOCK_BYTES (SPK_BLOCK_SAMPLES_PER_CH * SPK_NUM_CHANNELS * sizeof(int16_t))
#define SPK_BLOCK_COUNT 4U

K_MEM_SLAB_DEFINE(spk_tx_slab, SPK_BLOCK_BYTES, SPK_BLOCK_COUNT, 4);

static struct {
    bool inited;
    bool running;
    uint32_t sample_rate_hz;
} g_spk;

static int spk_gpio_init(void)
{
#if AMP_HAS_SD_MODE
    if (!device_is_ready(amp_sd_mode.port)) {
        return HAL_ENODEV;
    }
    int rc = gpio_pin_configure_dt(&amp_sd_mode, GPIO_OUTPUT_INACTIVE);
    if (rc) {
        return rc;
    }
#endif

#if AMP_HAS_PWR_EN
    if (!device_is_ready(amp_pwr_en.port)) {
        return HAL_ENODEV;
    }
    int rc = gpio_pin_configure_dt(&amp_pwr_en, GPIO_OUTPUT_INACTIVE);
    if (rc) {
        return rc;
    }
#endif

    return HAL_OK;
}

static int spk_i2s_configure(uint32_t sample_rate_hz)
{
    struct i2s_config cfg = {0};

    cfg.word_size = SPK_BITS_PER_SAMPLE;
    cfg.channels = SPK_NUM_CHANNELS;
    cfg.format = I2S_FMT_DATA_FORMAT_I2S;
    cfg.options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER;
    cfg.frame_clk_freq = sample_rate_hz;
    cfg.block_size = SPK_BLOCK_BYTES;
    cfg.mem_slab = &spk_tx_slab;
    cfg.timeout = 2000;

    return i2s_configure(i2s_dev, I2S_DIR_TX, &cfg);
}

static int spk_preload_and_start(void)
{
    (void)i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);

    for (int i = 0; i < 3; i++) {
        int16_t *blk = NULL;
        int rc = k_mem_slab_alloc(&spk_tx_slab, (void **)&blk, K_MSEC(100));
        if (rc) {
            LOG_ERR("i2s preload alloc failed: %d", rc);
            return rc;
        }
        memset(blk, 0, SPK_BLOCK_BYTES);

        rc = i2s_write(i2s_dev, blk, SPK_BLOCK_BYTES);
        if (rc) {
            k_mem_slab_free(&spk_tx_slab, (void *)blk);
            LOG_ERR("i2s preload write failed: %d", rc);
            return rc;
        }
    }

    int rc = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
    if (rc) {
        LOG_ERR("i2s start failed: %d", rc);
        return rc;
    }

    g_spk.running = true;
    return HAL_OK;
}

static int spk_nrf_init(void)
{
    if (g_spk.inited) {
        return HAL_OK;
    }

    if (!device_is_ready(i2s_dev)) {
        LOG_ERR("i2s0 not ready");
        return HAL_ENODEV;
    }

    int rc = spk_gpio_init();
    if (rc != HAL_OK) {
        LOG_ERR("amp gpio init failed: %d", rc);
        return rc;
    }

    uint32_t sample_rate = SPK_SAMPLE_RATE_HZ;
    rc = spk_i2s_configure(sample_rate);
    if (rc) {
        LOG_ERR("i2s configure failed: %d", rc);
        return rc;
    }

    g_spk.sample_rate_hz = sample_rate;
    g_spk.inited = true;
    return HAL_OK;
}

static int spk_set_power(bool enable)
{
    int rc = 0;
#if AMP_HAS_PWR_EN
    rc = gpio_pin_set_dt(&amp_pwr_en, enable ? 1 : 0);
    if (rc) {
        return rc;
    }
#endif
#if AMP_HAS_SD_MODE
    rc = gpio_pin_set_dt(&amp_sd_mode, enable ? 1 : 0);
    if (rc) {
        return rc;
    }
#endif

    return HAL_OK;
}

static int spk_nrf_start(void)
{
    if (!g_spk.inited) {
        return HAL_EINVAL;
    }
    if (g_spk.running) {
        return HAL_OK;
    }

    int rc = spk_set_power(true);
    if (rc) {
        return rc;
    }

    k_msleep(5);
    return spk_preload_and_start();
}

static int spk_nrf_stop(void)
{
    if (!g_spk.inited) {
        return HAL_EINVAL;
    }

    (void)i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_STOP);
    (void)i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
    (void)spk_set_power(false);
    g_spk.running = false;
    return HAL_OK;
}

static int spk_nrf_write(const void *buf, size_t len, int timeout_ms)
{
    if (!g_spk.running) {
        return HAL_EBUSY;
    }
    if (buf == NULL || len == 0U || (len % sizeof(int16_t)) != 0U) {
        return HAL_EINVAL;
    }

    const int16_t *samples = (const int16_t *)buf;
    size_t sample_count = len / sizeof(int16_t);

    size_t off = 0;
    while (off < sample_count) {
        size_t left = sample_count - off;
        size_t chunk = left > SPK_BLOCK_SAMPLES_PER_CH ? SPK_BLOCK_SAMPLES_PER_CH : left;

        int16_t *blk = NULL;
        int rc = k_mem_slab_alloc(&spk_tx_slab, (void **)&blk,
                                  timeout_ms < 0 ? K_FOREVER : K_MSEC(timeout_ms));
        if (rc) {
            return rc;
        }

        /* Mono PCM16 -> stereo interleave, zero-pad remaining if last block. */
        for (size_t i = 0; i < SPK_BLOCK_SAMPLES_PER_CH; i++) {
            int16_t s = (i < chunk) ? samples[off + i] : 0;
            blk[i * 2 + 0] = s;
            blk[i * 2 + 1] = s;
        }

        rc = i2s_write(i2s_dev, blk, SPK_BLOCK_BYTES);
        if (rc) {
            k_mem_slab_free(&spk_tx_slab, (void *)blk);
            return rc;
        }

        off += chunk;
    }

    return HAL_OK;
}

static const hal_spk_ops_t g_spk_ops = {
    .init = spk_nrf_init,
    .start = spk_nrf_start,
    .stop = spk_nrf_stop,
    .write = spk_nrf_write,
};

int spk_nrf_register(void)
{
    return hal_spk_register(&g_spk_ops);
}

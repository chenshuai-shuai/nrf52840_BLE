#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <string.h>

#include "boot_tone.h"
#include "hal_spk.h"
#include "error.h"

LOG_MODULE_REGISTER(boot_tone, LOG_LEVEL_INF);

#define BOOT_TONE_TARGET_PEAK 13500
#define BOOT_TONE_RAMP_SAMPLES 32

static int16_t boot_tone_limit_sample(int32_t sample)
{
    if (sample > 32767) {
        sample = 32767;
    } else if (sample < -32768) {
        sample = -32768;
    }
    return (int16_t)sample;
}

static int16_t boot_tone_shape_sample(int16_t sample, int idx, int total)
{
    int32_t shaped = ((int32_t)sample * BOOT_TONE_TARGET_PEAK) / 12140;

    /* Fade block edges slightly to reduce clicks without lowering the tone body. */
    if (idx < BOOT_TONE_RAMP_SAMPLES) {
        shaped = (shaped * (idx + 1)) / BOOT_TONE_RAMP_SAMPLES;
    } else if (idx >= (total - BOOT_TONE_RAMP_SAMPLES)) {
        int tail = total - idx;
        shaped = (shaped * tail) / BOOT_TONE_RAMP_SAMPLES;
    }

    return boot_tone_limit_sample(shaped);
}

static int boot_tone_write_block(const int16_t *buf, size_t len)
{
    int last_ret = -EAGAIN;
    for (int retry = 0; retry < 10; retry++) {
        int ret = hal_spk_write(buf, len, 200);
        if (ret == HAL_OK) {
            return HAL_OK;
        }
        last_ret = ret;
        if (ret == -EAGAIN || ret == -ENOMEM || ret == HAL_EBUSY) {
            k_msleep(5);
            continue;
        }
        return ret;
    }
    return last_ret;
}

void boot_tone_play(void)
{
#if !IS_ENABLED(CONFIG_BOOT_TONE)
    return;
#else
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
    /* Battery-only + 4 ohm speaker:
     * reduce low-frequency energy and shorten sustain to avoid rail droop. */
    const uint32_t tone_hz[] = { 1320U, 1760U, 2350U };
    const uint8_t tone_blocks[] = { 16, 16, 22 }; /* 10 ms per block */

    LOG_INF("boot tone: start");
    int64_t start_ms = k_uptime_get();
    for (size_t t = 0; t < ARRAY_SIZE(tone_hz); t++) {
        uint32_t phase = 0;
        uint32_t step = (tone_hz[t] * 32U << 16) / rate;

        for (int block = 0; block < tone_blocks[t]; block++) {
            for (int i = 0; i < 160; i++) {
                uint32_t idx = (phase >> 16) & 0x1FU;
                buf[i] = boot_tone_shape_sample(sine32[idx], i, ARRAY_SIZE(buf));
                phase += step;
            }
            ret = boot_tone_write_block(buf, sizeof(buf));
            if (ret != HAL_OK) {
                LOG_ERR("boot tone: hal_spk_write failed: %d", ret);
                (void)hal_spk_stop();
                return;
            }
        }

        memset(buf, 0, sizeof(buf));
        for (int block = 0; block < 5; block++) {
            ret = boot_tone_write_block(buf, sizeof(buf));
            if (ret != HAL_OK) {
                LOG_ERR("boot tone: silent write failed: %d", ret);
                (void)hal_spk_stop();
                return;
            }
        }
    }

    (void)hal_spk_stop();
    LOG_INF("boot tone: done (%d ms)", (int)(k_uptime_get() - start_ms));
#endif
}

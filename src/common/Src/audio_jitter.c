#include <zephyr/kernel.h>

#include "audio_jitter.h"

void audio_jitter_init(audio_jitter_t *j, uint32_t expected_period_us)
{
    if (j == NULL) {
        return;
    }

    j->expected_period_us = expected_period_us;
    j->last_ts_us = 0U;
    j->max_jitter_us = 0U;
    j->min_jitter_us = 0xFFFFFFFFU;
    j->samples = 0U;
}

void audio_jitter_update(audio_jitter_t *j, uint32_t now_us)
{
    if (j == NULL) {
        return;
    }

    if (j->last_ts_us != 0U) {
        uint32_t delta = now_us - j->last_ts_us;
        uint32_t jitter = (delta > j->expected_period_us)
            ? (delta - j->expected_period_us)
            : (j->expected_period_us - delta);

        if (j->samples == 0U) {
            j->min_jitter_us = jitter;
            j->max_jitter_us = jitter;
        } else {
            if (jitter < j->min_jitter_us) {
                j->min_jitter_us = jitter;
            }
            if (jitter > j->max_jitter_us) {
                j->max_jitter_us = jitter;
            }
        }
        j->samples++;
    }

    j->last_ts_us = now_us;
}

#ifndef COMMON_AUDIO_JITTER_H
#define COMMON_AUDIO_JITTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t expected_period_us;
    uint32_t last_ts_us;
    uint32_t max_jitter_us;
    uint32_t min_jitter_us;
    uint32_t samples;
} audio_jitter_t;

void audio_jitter_init(audio_jitter_t *j, uint32_t expected_period_us);
void audio_jitter_update(audio_jitter_t *j, uint32_t now_us);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_AUDIO_JITTER_H */

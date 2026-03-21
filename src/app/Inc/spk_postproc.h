#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int spk_postproc_init(void);
void spk_postproc_reset(void);
int spk_postproc_process_frame(const int16_t *in_pcm,
                               size_t in_samples,
                               uint32_t in_rate_hz,
                               int16_t *out_pcm,
                               size_t out_cap_samples,
                               size_t *out_samples,
                               bool fade_out_tail);

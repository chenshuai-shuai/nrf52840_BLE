#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t frames;
    uint32_t samples;
    uint16_t in_rms;
    uint16_t out_rms;
    uint16_t active_rms;
    uint16_t idle_rms;
    uint16_t low_removed_pm;
    uint16_t gate_attn_pm;
    uint16_t comp_attn_pm;
    uint16_t clip_pm;
    int16_t dc_out;
} spk_postproc_diag_t;

int spk_postproc_init(void);
void spk_postproc_reset(void);
int spk_postproc_process_frame(const int16_t *in_pcm,
                               size_t in_samples,
                               uint32_t in_rate_hz,
                               int16_t *out_pcm,
                               size_t out_cap_samples,
                               size_t *out_samples,
                               bool fade_out_tail);
void spk_postproc_get_diag(spk_postproc_diag_t *out);

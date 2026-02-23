#pragma once

#include <stdbool.h>

typedef enum {
    AUDIO_MODE_BOOT = 0,
    AUDIO_MODE_UPLOAD = 1,
    AUDIO_MODE_PLAY = 2,
} audio_mode_t;

void app_state_init(audio_mode_t mode);
void app_state_set(audio_mode_t mode);
audio_mode_t app_state_get(void);
bool app_state_mic_enabled(void);

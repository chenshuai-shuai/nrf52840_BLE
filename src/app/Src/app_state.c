#include <zephyr/sys/atomic.h>

#include "app_state.h"

static atomic_t g_audio_mode;
static volatile bool g_mic_upload_enabled = true;

void app_state_init(audio_mode_t mode)
{
    app_state_set(mode);
}

void app_state_set(audio_mode_t mode)
{
    atomic_set(&g_audio_mode, (int)mode);
    if (mode == AUDIO_MODE_PLAY) {
        g_mic_upload_enabled = false;
    } else if (mode == AUDIO_MODE_UPLOAD) {
        g_mic_upload_enabled = true;
    }
}

audio_mode_t app_state_get(void)
{
    return (audio_mode_t)atomic_get(&g_audio_mode);
}

bool app_state_mic_enabled(void)
{
    return g_mic_upload_enabled;
}

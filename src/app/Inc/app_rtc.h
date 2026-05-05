#pragma once

#include <stdbool.h>

void app_rtc_init(void);
void app_rtc_start(void);
void app_rtc_playback_critical_enter(void);
void app_rtc_playback_critical_exit(void);
int app_rtc_audio_suspend(void);
int app_rtc_audio_resume(void);
bool app_rtc_audio_is_suspended(void);

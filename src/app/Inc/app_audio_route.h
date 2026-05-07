#ifndef APP_AUDIO_ROUTE_H
#define APP_AUDIO_ROUTE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_AUDIO_STATE_SAFE_HANDOFF = 0,
    APP_AUDIO_STATE_NRF_AUDIO_OWNER,
    APP_AUDIO_STATE_ESP_AUDIO_OWNER,
    APP_AUDIO_STATE_NRF_ESP_BOOTCTRL,
} app_audio_route_state_t;

int app_audio_route_init(void);
app_audio_route_state_t app_audio_route_get_state(void);

int app_audio_route_enter_safe(void);
int app_audio_route_enter_bootctrl(void);
int app_audio_route_acquire_bootctrl(void);
int app_audio_route_finish_bootctrl(void);
int app_audio_route_force_nrf_audio(void);

int app_audio_route_request_nrf_audio(void);
int app_audio_route_request_esp_audio(void);

bool app_audio_route_is_nrf_owner(void);
bool app_audio_route_is_esp_owner(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_AUDIO_ROUTE_H */

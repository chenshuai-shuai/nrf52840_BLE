#ifndef APP_ESP_LINK_H
#define APP_ESP_LINK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_ESP_STATE_UNKNOWN = 0,
    APP_ESP_STATE_SAFE_HANDOFF,
    APP_ESP_STATE_NRF_AUDIO_OWNER,
    APP_ESP_STATE_ESP_AUDIO_OWNER,
    APP_ESP_STATE_BOOTCTRL,
    APP_ESP_STATE_FAULT,
} app_esp_link_state_t;

int app_esp_link_init(void);
int app_esp_link_start(void);
bool app_esp_link_is_started(void);

int app_esp_link_ping(void);
bool app_esp_link_is_ready(void);

int app_esp_link_query_state(app_esp_link_state_t *state);
app_esp_link_state_t app_esp_link_cached_state(void);

int app_esp_link_request_audio_release(void);
int app_esp_link_request_audio_take(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_ESP_LINK_H */

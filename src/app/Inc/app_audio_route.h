#ifndef APP_AUDIO_ROUTE_H
#define APP_AUDIO_ROUTE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_AUDIO_OWNER_NONE = 0,
    APP_AUDIO_OWNER_NRF,
    APP_AUDIO_OWNER_WIFI,
} app_audio_owner_t;

int app_audio_route_init(void);
int app_audio_route_request_wifi(void);
int app_audio_route_release_wifi(void);
app_audio_owner_t app_audio_route_owner_get(void);
bool app_audio_route_is_wifi_active(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_AUDIO_ROUTE_H */

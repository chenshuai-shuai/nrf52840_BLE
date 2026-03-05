#ifndef APP_BUS_H
#define APP_BUS_H

#include <stddef.h>
#include <stdint.h>

#include "system_state.h"
#include "hal_ppg.h"
#include "app_lifecycle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_EVT_PM_STATE = 0,
    APP_EVT_PPG_HR,
    APP_EVT_GPS_FIX,
    APP_EVT_APP_LIFECYCLE,
    APP_EVT_COUNT
} app_event_id_t;

typedef struct {
    app_event_id_t id;
    uint32_t timestamp_ms;
    union {
        pm_state_t pm;
        hal_ppg_sample_t ppg;
        gps_state_t gps;
        struct {
            app_lifecycle_id_t app_id;
            app_lifecycle_status_t status;
        } lifecycle;
    } data;
} app_event_t;

typedef void (*app_event_cb_t)(const app_event_t *evt, void *user);

int app_bus_start(void);
int app_bus_subscribe(app_event_id_t id, app_event_cb_t cb, void *user);
int app_bus_publish(const app_event_t *evt);

#ifdef __cplusplus
}
#endif

#endif /* APP_BUS_H */

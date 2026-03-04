#ifndef APP_UPLINK_SERVICE_H
#define APP_UPLINK_SERVICE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "app_data_store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_UPLINK_PRIO_HIGH = 0,
    APP_UPLINK_PRIO_NORMAL,
    APP_UPLINK_PRIO_LOW,
} app_uplink_prio_t;

int app_uplink_service_start(void);
int app_uplink_service_stop(void);
bool app_uplink_service_is_ready(void);

int app_uplink_publish(app_data_part_t part,
                       app_uplink_prio_t prio,
                       const void *payload,
                       size_t len,
                       uint32_t ts_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_UPLINK_SERVICE_H */

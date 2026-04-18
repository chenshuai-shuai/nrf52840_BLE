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
int app_uplink_service_pause(void);
int app_uplink_service_resume(void);
bool app_uplink_service_is_ready(void);
size_t app_uplink_max_payload(void);
int app_uplink_take_downlink(app_data_record_t *out, int timeout_ms);

/**
 * @brief Set RX-priority mode for audio playback.
 *
 * When enabled the uplink thread skips all TX work and drains BLE RX
 * as fast as possible so that audio packets reach the speaker ring
 * buffer without delay.
 */
void app_uplink_set_rx_priority(bool enable);

int app_uplink_publish(app_data_part_t part,
                       app_uplink_prio_t prio,
                       const void *payload,
                       size_t len,
                       uint32_t ts_ms);

int app_uplink_publish_batch(app_data_part_t part,
                             app_uplink_prio_t prio,
                             const uint8_t *const *payloads,
                             const uint16_t *lens,
                             size_t count,
                             uint32_t ts_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_UPLINK_SERVICE_H */

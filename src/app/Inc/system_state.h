#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <zephyr/kernel.h>
#include "types.h"
#include "hal_ppg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t soc_x100;
    uint16_t vcell_mv;
    int16_t current_ma;
    uint8_t chg_dtls;
    uint8_t chgin_dtls;
    uint8_t chg;
    uint8_t time_sus;
    uint32_t timestamp_ms;
    uint8_t valid;
} pm_state_t;

typedef struct {
    hal_ppg_sample_t sample;
    uint32_t timestamp_ms;
    uint8_t valid;
} ppg_state_t;

typedef struct {
    double lat_deg;
    double lon_deg;
    float speed_kmh;
    uint8_t fix_valid;
    uint8_t sats;
    uint32_t timestamp_ms;
    uint8_t valid;
} gps_state_t;

void system_state_init(void);
void system_state_set_pm(const pm_state_t *state);
int system_state_get_pm(pm_state_t *out);
void system_state_set_ppg(const ppg_state_t *state);
int system_state_get_ppg(ppg_state_t *out);
void system_state_set_gps(const gps_state_t *state);
int system_state_get_gps(gps_state_t *out);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_STATE_H */

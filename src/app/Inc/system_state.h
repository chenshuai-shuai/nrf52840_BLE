#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <zephyr/kernel.h>
#include "types.h"

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

void system_state_init(void);
void system_state_set_pm(const pm_state_t *state);
int system_state_get_pm(pm_state_t *out);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_STATE_H */

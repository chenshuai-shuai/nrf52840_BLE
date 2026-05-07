#ifndef PLATFORM_SHARED_BUS_H
#define PLATFORM_SHARED_BUS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLATFORM_SHARED_BUS_SAFE_HANDOFF = 0,
    PLATFORM_SHARED_BUS_NRF_AUDIO,
    PLATFORM_SHARED_BUS_NRF_BOOTCTRL,
} platform_shared_bus_mode_t;

int platform_shared_bus_init(void);
platform_shared_bus_mode_t platform_shared_bus_get_mode(void);

int platform_shared_bus_enter_safe_handoff(void);
int platform_shared_bus_enter_nrf_audio(void);
int platform_shared_bus_enter_nrf_bootctrl(void);

int platform_shared_bus_set_amp_sd(bool enable);
int platform_shared_bus_set_boot_signal(bool boot_asserted);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_SHARED_BUS_H */

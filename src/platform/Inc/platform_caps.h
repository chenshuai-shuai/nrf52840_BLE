#ifndef PLATFORM_CAPS_H
#define PLATFORM_CAPS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool audio;
    bool ppg;
    bool imu;
    bool flash;
    bool gps;
    bool ble;
    bool mic;
    bool spk;
    bool pm;
} platform_caps_t;

int platform_caps_set(const platform_caps_t *caps);
const platform_caps_t *platform_caps_get(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_CAPS_H */

#ifndef HAL_TEMP_H
#define HAL_TEMP_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int16_t raw;
    int32_t micro_c;
    float celsius;
    uint32_t timestamp_ms;
} hal_temp_sample_t;

typedef struct {
    int (*init)(void);
    int (*read)(hal_temp_sample_t *out, int timeout_ms);
    int (*get_device_id)(uint16_t *out);
} hal_temp_ops_t;

int hal_temp_register(const hal_temp_ops_t *ops);
int hal_temp_init(void);
int hal_temp_read(hal_temp_sample_t *out, int timeout_ms);
int hal_temp_get_device_id(uint16_t *out);

#ifdef __cplusplus
}
#endif

#endif /* HAL_TEMP_H */

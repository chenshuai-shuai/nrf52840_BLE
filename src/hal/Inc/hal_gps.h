#ifndef HAL_GPS_H
#define HAL_GPS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int (*init)(void);
    int (*start)(void);
    int (*stop)(void);
    int (*read)(void *buf, size_t len, int timeout_ms);
} hal_gps_ops_t;

int hal_gps_register(const hal_gps_ops_t *ops);
int hal_gps_init(void);
int hal_gps_start(void);
int hal_gps_stop(void);
int hal_gps_read(void *buf, size_t len, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* HAL_GPS_H */

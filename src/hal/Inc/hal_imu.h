#ifndef HAL_IMU_H
#define HAL_IMU_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int (*init)(void);
    int (*read)(void *buf, size_t len, int timeout_ms);
} hal_imu_ops_t;

typedef struct {
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
    int16_t temp;
} imu_sample_t;

int hal_imu_register(const hal_imu_ops_t *ops);
int hal_imu_init(void);
int hal_imu_read(void *buf, size_t len, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* HAL_IMU_H */

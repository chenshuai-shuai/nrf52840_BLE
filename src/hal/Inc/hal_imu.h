#ifndef HAL_IMU_H
#define HAL_IMU_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
    int16_t temp;
} imu_sample_t;

typedef struct {
    int (*init)(void);
    int (*read)(void *buf, size_t len, int timeout_ms);
    int (*read_timed)(void *buf, size_t len, uint64_t *timestamp_us, int timeout_ms);
    int (*get_latest)(imu_sample_t *out, uint32_t *timestamp_ms);
    int (*get_latest_us)(imu_sample_t *out, uint64_t *timestamp_us);
} hal_imu_ops_t;

int hal_imu_register(const hal_imu_ops_t *ops);
int hal_imu_init(void);
int hal_imu_read(void *buf, size_t len, int timeout_ms);
int hal_imu_read_timed(void *buf, size_t len, uint64_t *timestamp_us, int timeout_ms);
int hal_imu_get_latest(imu_sample_t *out, uint32_t *timestamp_ms);
int hal_imu_get_latest_us(imu_sample_t *out, uint64_t *timestamp_us);

#ifdef __cplusplus
}
#endif

#endif /* HAL_IMU_H */

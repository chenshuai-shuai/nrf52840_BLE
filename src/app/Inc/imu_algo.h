#ifndef IMU_ALGO_H
#define IMU_ALGO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IMU_ACTION_UNKNOWN = 0,
    IMU_ACTION_STILL,
    IMU_ACTION_LIGHT_MOVE,
    IMU_ACTION_WALK_LIKE,
    IMU_ACTION_RUN_LIKE,
    IMU_ACTION_VIGOROUS_MOVE,
} imu_action_t;

typedef struct {
    int32_t accel_x_lsb;
    int32_t accel_y_lsb;
    int32_t accel_z_lsb;
    int32_t gyro_x_lsb;
    int32_t gyro_y_lsb;
    int32_t gyro_z_lsb;
    int32_t temp_centi_deg;
    uint32_t timestamp_ms;
    uint32_t dt_ms;
} imu_algo_input_t;

typedef struct {
    imu_action_t action;
    uint8_t confidence; /* 0~100 */
    uint8_t valid;      /* 0 invalid, 1 valid */
} imu_algo_output_t;

typedef struct {
    int (*init)(void);
    int (*reset)(void);
    int (*process)(const imu_algo_input_t *in, imu_algo_output_t *out);
} imu_algo_ops_t;

int imu_algo_register(const imu_algo_ops_t *ops);
int imu_algo_init(void);
int imu_algo_reset(void);
int imu_algo_process(const imu_algo_input_t *in, imu_algo_output_t *out);
const char *imu_action_name(imu_action_t action);

#ifdef __cplusplus
}
#endif

#endif /* IMU_ALGO_H */

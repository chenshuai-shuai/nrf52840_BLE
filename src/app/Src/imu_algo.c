#include <string.h>

#include "imu_algo.h"
#include "error.h"

/*
 * Default local algorithm (rule-based fallback):
 * - Keep this as baseline so upper-layer interface is stable.
 * - External vendor algorithm can replace it through imu_algo_register().
 */

typedef struct {
    bool inited;
} imu_algo_ctx_t;

static imu_algo_ctx_t g_default_ctx;
static const imu_algo_ops_t *g_ops;

static int imu_algo_default_init(void)
{
    g_default_ctx.inited = true;
    return HAL_OK;
}

static int imu_algo_default_reset(void)
{
    g_default_ctx.inited = true;
    return HAL_OK;
}

static uint32_t iabs32(int32_t v)
{
    return (uint32_t)((v < 0) ? -v : v);
}

static int imu_algo_default_process(const imu_algo_input_t *in, imu_algo_output_t *out)
{
    if (!g_default_ctx.inited || in == NULL || out == NULL) {
        return HAL_EINVAL;
    }

    uint32_t gyro_sum = iabs32(in->gyro_x_lsb) + iabs32(in->gyro_y_lsb) + iabs32(in->gyro_z_lsb);
    uint32_t accel_dyn = iabs32(in->accel_x_lsb) + iabs32(in->accel_y_lsb) + iabs32(in->accel_z_lsb);

    out->valid = 1;
    out->confidence = 70;

    if (gyro_sum < 90U && accel_dyn < 260U) {
        out->action = IMU_ACTION_STILL;
        out->confidence = 95;
    } else if (gyro_sum < 300U && accel_dyn < 600U) {
        out->action = IMU_ACTION_LIGHT_MOVE;
        out->confidence = 85;
    } else if (gyro_sum < 900U && accel_dyn < 1500U) {
        out->action = IMU_ACTION_WALK_LIKE;
        out->confidence = 80;
    } else if (gyro_sum < 2200U && accel_dyn < 3200U) {
        out->action = IMU_ACTION_RUN_LIKE;
        out->confidence = 75;
    } else {
        out->action = IMU_ACTION_VIGOROUS_MOVE;
        out->confidence = 70;
    }

    return HAL_OK;
}

static const imu_algo_ops_t g_default_ops = {
    .init = imu_algo_default_init,
    .reset = imu_algo_default_reset,
    .process = imu_algo_default_process,
};

int imu_algo_register(const imu_algo_ops_t *ops)
{
    if (ops == NULL || ops->init == NULL || ops->process == NULL) {
        return HAL_EINVAL;
    }
    g_ops = ops;
    return HAL_OK;
}

int imu_algo_init(void)
{
    if (g_ops == NULL) {
        g_ops = &g_default_ops;
    }
    return g_ops->init();
}

int imu_algo_reset(void)
{
    if (g_ops == NULL) {
        g_ops = &g_default_ops;
    }
    if (g_ops->reset == NULL) {
        return HAL_OK;
    }
    return g_ops->reset();
}

int imu_algo_process(const imu_algo_input_t *in, imu_algo_output_t *out)
{
    if (g_ops == NULL) {
        g_ops = &g_default_ops;
        if (g_ops->init) {
            (void)g_ops->init();
        }
    }
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    return g_ops->process(in, out);
}

const char *imu_action_name(imu_action_t action)
{
    switch (action) {
    case IMU_ACTION_STILL:
        return "still";
    case IMU_ACTION_LIGHT_MOVE:
        return "light_move";
    case IMU_ACTION_WALK_LIKE:
        return "walk_like";
    case IMU_ACTION_RUN_LIKE:
        return "run_like";
    case IMU_ACTION_VIGOROUS_MOVE:
        return "vigorous_move";
    case IMU_ACTION_UNKNOWN:
    default:
        return "unknown";
    }
}

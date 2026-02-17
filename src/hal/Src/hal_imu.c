#include "hal_imu.h"
#include "error.h"

static const hal_imu_ops_t *g_imu_ops;

int hal_imu_register(const hal_imu_ops_t *ops)
{
    if (ops == NULL || ops->init == NULL) {
        return HAL_EINVAL;
    }
    if (g_imu_ops != NULL) {
        return HAL_EBUSY;
    }
    g_imu_ops = ops;
    return HAL_OK;
}

int hal_imu_init(void)
{
    if (g_imu_ops == NULL || g_imu_ops->init == NULL) {
        return HAL_ENODEV;
    }
    return g_imu_ops->init();
}

int hal_imu_read(void *buf, size_t len, int timeout_ms)
{
    if (g_imu_ops == NULL || g_imu_ops->read == NULL) {
        return HAL_ENODEV;
    }
    return g_imu_ops->read(buf, len, timeout_ms);
}

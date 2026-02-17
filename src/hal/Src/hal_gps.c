#include "hal_gps.h"
#include "error.h"

static const hal_gps_ops_t *g_gps_ops;

int hal_gps_register(const hal_gps_ops_t *ops)
{
    if (ops == NULL || ops->init == NULL) {
        return HAL_EINVAL;
    }
    if (g_gps_ops != NULL) {
        return HAL_EBUSY;
    }
    g_gps_ops = ops;
    return HAL_OK;
}

int hal_gps_init(void)
{
    if (g_gps_ops == NULL || g_gps_ops->init == NULL) {
        return HAL_ENODEV;
    }
    return g_gps_ops->init();
}

int hal_gps_start(void)
{
    if (g_gps_ops == NULL || g_gps_ops->start == NULL) {
        return HAL_ENODEV;
    }
    return g_gps_ops->start();
}

int hal_gps_stop(void)
{
    if (g_gps_ops == NULL || g_gps_ops->stop == NULL) {
        return HAL_ENODEV;
    }
    return g_gps_ops->stop();
}

int hal_gps_read(void *buf, size_t len, int timeout_ms)
{
    if (g_gps_ops == NULL || g_gps_ops->read == NULL) {
        return HAL_ENODEV;
    }
    return g_gps_ops->read(buf, len, timeout_ms);
}

#include "hal_ppg.h"
#include "error.h"

static const hal_ppg_ops_t *g_ppg_ops;

int hal_ppg_register(const hal_ppg_ops_t *ops)
{
    if (ops == NULL || ops->init == NULL) {
        return HAL_EINVAL;
    }
    if (g_ppg_ops != NULL) {
        return HAL_EBUSY;
    }
    g_ppg_ops = ops;
    return HAL_OK;
}

int hal_ppg_init(void)
{
    if (g_ppg_ops == NULL || g_ppg_ops->init == NULL) {
        return HAL_ENODEV;
    }
    return g_ppg_ops->init();
}

int hal_ppg_start(void)
{
    if (g_ppg_ops == NULL || g_ppg_ops->start == NULL) {
        return HAL_ENODEV;
    }
    return g_ppg_ops->start();
}

int hal_ppg_stop(void)
{
    if (g_ppg_ops == NULL || g_ppg_ops->stop == NULL) {
        return HAL_ENODEV;
    }
    return g_ppg_ops->stop();
}

int hal_ppg_read(void *buf, size_t len, int timeout_ms)
{
    if (g_ppg_ops == NULL || g_ppg_ops->read == NULL) {
        return HAL_ENODEV;
    }
    return g_ppg_ops->read(buf, len, timeout_ms);
}

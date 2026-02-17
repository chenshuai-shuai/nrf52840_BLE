#include "hal_spk.h"
#include "error.h"

static const hal_spk_ops_t *g_spk_ops;

int hal_spk_register(const hal_spk_ops_t *ops)
{
    if (ops == NULL || ops->init == NULL) {
        return HAL_EINVAL;
    }
    if (g_spk_ops != NULL) {
        return HAL_EBUSY;
    }
    g_spk_ops = ops;
    return HAL_OK;
}

int hal_spk_init(void)
{
    if (g_spk_ops == NULL || g_spk_ops->init == NULL) {
        return HAL_ENODEV;
    }
    return g_spk_ops->init();
}

int hal_spk_start(void)
{
    if (g_spk_ops == NULL || g_spk_ops->start == NULL) {
        return HAL_ENODEV;
    }
    return g_spk_ops->start();
}

int hal_spk_stop(void)
{
    if (g_spk_ops == NULL || g_spk_ops->stop == NULL) {
        return HAL_ENODEV;
    }
    return g_spk_ops->stop();
}

int hal_spk_write(const void *buf, size_t len, int timeout_ms)
{
    if (g_spk_ops == NULL || g_spk_ops->write == NULL) {
        return HAL_ENODEV;
    }
    return g_spk_ops->write(buf, len, timeout_ms);
}

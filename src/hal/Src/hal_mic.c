#include "hal_mic.h"
#include "error.h"

static const hal_mic_ops_t *g_mic_ops;

int hal_mic_register(const hal_mic_ops_t *ops)
{
    if (ops == NULL || ops->init == NULL) {
        return HAL_EINVAL;
    }
    if (g_mic_ops != NULL) {
        return HAL_EBUSY;
    }
    g_mic_ops = ops;
    return HAL_OK;
}

int hal_mic_init(void)
{
    if (g_mic_ops == NULL || g_mic_ops->init == NULL) {
        return HAL_ENODEV;
    }
    return g_mic_ops->init();
}

int hal_mic_start(void)
{
    if (g_mic_ops == NULL || g_mic_ops->start == NULL) {
        return HAL_ENODEV;
    }
    return g_mic_ops->start();
}

int hal_mic_stop(void)
{
    if (g_mic_ops == NULL || g_mic_ops->stop == NULL) {
        return HAL_ENODEV;
    }
    return g_mic_ops->stop();
}

int hal_mic_read_block(void **buf, size_t *len, int timeout_ms)
{
    if (g_mic_ops == NULL || g_mic_ops->read_block == NULL) {
        return HAL_ENODEV;
    }
    return g_mic_ops->read_block(buf, len, timeout_ms);
}

int hal_mic_release(void *buf)
{
    if (g_mic_ops == NULL || g_mic_ops->release == NULL) {
        return HAL_ENODEV;
    }
    return g_mic_ops->release(buf);
}

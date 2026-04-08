#include "hal_temp.h"
#include "error.h"

static const hal_temp_ops_t *g_temp_ops;

int hal_temp_register(const hal_temp_ops_t *ops)
{
    if (ops == NULL || ops->init == NULL || ops->read == NULL) {
        return HAL_EINVAL;
    }
    if (g_temp_ops != NULL) {
        return HAL_EBUSY;
    }
    g_temp_ops = ops;
    return HAL_OK;
}

int hal_temp_init(void)
{
    if (g_temp_ops == NULL || g_temp_ops->init == NULL) {
        return HAL_ENODEV;
    }
    return g_temp_ops->init();
}

int hal_temp_read(hal_temp_sample_t *out, int timeout_ms)
{
    if (g_temp_ops == NULL || g_temp_ops->read == NULL) {
        return HAL_ENODEV;
    }
    return g_temp_ops->read(out, timeout_ms);
}

int hal_temp_get_device_id(uint16_t *out)
{
    if (g_temp_ops == NULL || g_temp_ops->get_device_id == NULL) {
        return HAL_ENOTSUP;
    }
    return g_temp_ops->get_device_id(out);
}

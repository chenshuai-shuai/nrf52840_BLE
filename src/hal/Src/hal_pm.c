#include "hal_pm.h"
#include "error.h"

static const hal_pm_ops_t *g_pm_ops;

int hal_pm_register(const hal_pm_ops_t *ops)
{
    if (ops == NULL || ops->init == NULL) {
        return HAL_EINVAL;
    }
    if (g_pm_ops != NULL) {
        return HAL_EBUSY;
    }
    g_pm_ops = ops;
    return HAL_OK;
}

int hal_pm_init(void)
{
    if (g_pm_ops == NULL || g_pm_ops->init == NULL) {
        return HAL_ENODEV;
    }
    return g_pm_ops->init();
}

int hal_pm_set_mode(int mode)
{
    if (g_pm_ops == NULL || g_pm_ops->set_mode == NULL) {
        return HAL_ENODEV;
    }
    return g_pm_ops->set_mode(mode);
}

int hal_pm_get_status(int *status)
{
    if (g_pm_ops == NULL || g_pm_ops->get_status == NULL) {
        return HAL_ENODEV;
    }
    return g_pm_ops->get_status(status);
}

#include <zephyr/kernel.h>

#include "hal_pm.h"
#include "error.h"

static int pm_nrf_init(void)
{

    return HAL_OK;
}

static int pm_nrf_set_mode(int mode)
{
    ARG_UNUSED(mode);

    return HAL_ENOTSUP;
}

static int pm_nrf_get_status(int *status)
{
    ARG_UNUSED(status);

    return HAL_ENOTSUP;
}

static const hal_pm_ops_t g_pm_ops = {
    .init = pm_nrf_init,
    .set_mode = pm_nrf_set_mode,
    .get_status = pm_nrf_get_status,
};

int pm_nrf_register(void)
{
    return hal_pm_register(&g_pm_ops);
}

#include <zephyr/kernel.h>

#include "hal_imu.h"
#include "error.h"

static int imu_nrf_init(void)
{

    return HAL_OK;
}

static int imu_nrf_read(void *buf, size_t len, int timeout_ms)
{
    ARG_UNUSED(buf);
    ARG_UNUSED(len);
    ARG_UNUSED(timeout_ms);

    return HAL_ENOTSUP;
}

static const hal_imu_ops_t g_imu_ops = {
    .init = imu_nrf_init,
    .read = imu_nrf_read,
};

int imu_nrf_register(void)
{
    return hal_imu_register(&g_imu_ops);
}

#include <zephyr/kernel.h>

#include "hal_gps.h"
#include "error.h"

static int gps_nrf_init(void)
{

    return HAL_OK;
}

static int gps_nrf_start(void)
{

    return HAL_ENOTSUP;
}

static int gps_nrf_stop(void)
{

    return HAL_ENOTSUP;
}

static int gps_nrf_read(void *buf, size_t len, int timeout_ms)
{
    ARG_UNUSED(buf);
    ARG_UNUSED(len);
    ARG_UNUSED(timeout_ms);

    return HAL_ENOTSUP;
}

static const hal_gps_ops_t g_gps_ops = {
    .init = gps_nrf_init,
    .start = gps_nrf_start,
    .stop = gps_nrf_stop,
    .read = gps_nrf_read,
};

int gps_nrf_register(void)
{
    return hal_gps_register(&g_gps_ops);
}

#include <zephyr/kernel.h>

#include "hal_ppg.h"
#include "error.h"

static int ppg_nrf_init(void)
{

    return HAL_OK;
}

static int ppg_nrf_start(void)
{

    return HAL_ENOTSUP;
}

static int ppg_nrf_stop(void)
{

    return HAL_ENOTSUP;
}

static int ppg_nrf_read(void *buf, size_t len, int timeout_ms)
{
    ARG_UNUSED(buf);
    ARG_UNUSED(len);
    ARG_UNUSED(timeout_ms);

    return HAL_ENOTSUP;
}

static const hal_ppg_ops_t g_ppg_ops = {
    .init = ppg_nrf_init,
    .start = ppg_nrf_start,
    .stop = ppg_nrf_stop,
    .read = ppg_nrf_read,
};

int ppg_nrf_register(void)
{
    return hal_ppg_register(&g_ppg_ops);
}

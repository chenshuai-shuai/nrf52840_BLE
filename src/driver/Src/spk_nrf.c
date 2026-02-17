#include <zephyr/kernel.h>

#include "hal_spk.h"
#include "error.h"

static int spk_nrf_init(void)
{

    return HAL_OK;
}

static int spk_nrf_start(void)
{

    return HAL_ENOTSUP;
}

static int spk_nrf_stop(void)
{

    return HAL_ENOTSUP;
}

static int spk_nrf_write(const void *buf, size_t len, int timeout_ms)
{
    ARG_UNUSED(buf);
    ARG_UNUSED(len);
    ARG_UNUSED(timeout_ms);

    return HAL_ENOTSUP;
}

static const hal_spk_ops_t g_spk_ops = {
    .init = spk_nrf_init,
    .start = spk_nrf_start,
    .stop = spk_nrf_stop,
    .write = spk_nrf_write,
};

int spk_nrf_register(void)
{
    return hal_spk_register(&g_spk_ops);
}

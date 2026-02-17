#include <zephyr/kernel.h>

#include "hal_mic.h"
#include "error.h"

static int mic_nrf_init(void)
{

    return HAL_OK;
}

static int mic_nrf_start(void)
{

    return HAL_ENOTSUP;
}

static int mic_nrf_stop(void)
{

    return HAL_ENOTSUP;
}

static int mic_nrf_read_block(void **buf, size_t *len, int timeout_ms)
{
    ARG_UNUSED(buf);
    ARG_UNUSED(len);
    ARG_UNUSED(timeout_ms);

    return HAL_ENOTSUP;
}

static int mic_nrf_release(void *buf)
{
    ARG_UNUSED(buf);

    return HAL_ENOTSUP;
}

static const hal_mic_ops_t g_mic_ops = {
    .init = mic_nrf_init,
    .start = mic_nrf_start,
    .stop = mic_nrf_stop,
    .read_block = mic_nrf_read_block,
    .release = mic_nrf_release,
};

int mic_nrf_register(void)
{
    return hal_mic_register(&g_mic_ops);
}

#include <zephyr/kernel.h>

#include "hal_ble.h"
#include "error.h"

static int ble_nrf_init(void)
{

    return HAL_OK;
}

static int ble_nrf_start(void)
{

    return HAL_ENOTSUP;
}

static int ble_nrf_stop(void)
{

    return HAL_ENOTSUP;
}

static int ble_nrf_send(const void *buf, size_t len, int timeout_ms)
{
    ARG_UNUSED(buf);
    ARG_UNUSED(len);
    ARG_UNUSED(timeout_ms);

    return HAL_ENOTSUP;
}

static int ble_nrf_recv(void *buf, size_t len, int timeout_ms)
{
    ARG_UNUSED(buf);
    ARG_UNUSED(len);
    ARG_UNUSED(timeout_ms);

    return HAL_ENOTSUP;
}

static const hal_ble_ops_t g_ble_ops = {
    .init = ble_nrf_init,
    .start = ble_nrf_start,
    .stop = ble_nrf_stop,
    .send = ble_nrf_send,
    .recv = ble_nrf_recv,
};

int ble_nrf_register(void)
{
    return hal_ble_register(&g_ble_ops);
}

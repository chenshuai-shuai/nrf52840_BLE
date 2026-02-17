#include <zephyr/kernel.h>

#include "hal_flash.h"
#include "error.h"

static int flash_nrf_init(void)
{

    return HAL_OK;
}

static int flash_nrf_read(uint32_t addr, void *buf, size_t len)
{
    ARG_UNUSED(addr);
    ARG_UNUSED(buf);
    ARG_UNUSED(len);

    return HAL_ENOTSUP;
}

static int flash_nrf_write(uint32_t addr, const void *buf, size_t len)
{
    ARG_UNUSED(addr);
    ARG_UNUSED(buf);
    ARG_UNUSED(len);

    return HAL_ENOTSUP;
}

static int flash_nrf_erase(uint32_t addr, size_t len)
{
    ARG_UNUSED(addr);
    ARG_UNUSED(len);

    return HAL_ENOTSUP;
}

static const hal_flash_ops_t g_flash_ops = {
    .init = flash_nrf_init,
    .read = flash_nrf_read,
    .write = flash_nrf_write,
    .erase = flash_nrf_erase,
};

int flash_nrf_register(void)
{
    return hal_flash_register(&g_flash_ops);
}

#include "hal_flash.h"
#include "error.h"

static const hal_flash_ops_t *g_flash_ops;

int hal_flash_register(const hal_flash_ops_t *ops)
{
    if (ops == NULL || ops->init == NULL) {
        return HAL_EINVAL;
    }
    if (g_flash_ops != NULL) {
        return HAL_EBUSY;
    }
    g_flash_ops = ops;
    return HAL_OK;
}

int hal_flash_init(void)
{
    if (g_flash_ops == NULL || g_flash_ops->init == NULL) {
        return HAL_ENODEV;
    }
    return g_flash_ops->init();
}

int hal_flash_read(uint32_t addr, void *buf, size_t len)
{
    if (g_flash_ops == NULL || g_flash_ops->read == NULL) {
        return HAL_ENODEV;
    }
    return g_flash_ops->read(addr, buf, len);
}

int hal_flash_write(uint32_t addr, const void *buf, size_t len)
{
    if (g_flash_ops == NULL || g_flash_ops->write == NULL) {
        return HAL_ENODEV;
    }
    return g_flash_ops->write(addr, buf, len);
}

int hal_flash_erase(uint32_t addr, size_t len)
{
    if (g_flash_ops == NULL || g_flash_ops->erase == NULL) {
        return HAL_ENODEV;
    }
    return g_flash_ops->erase(addr, len);
}

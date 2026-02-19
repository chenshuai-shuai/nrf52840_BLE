#include "hal_ble.h"
#include "error.h"

static const hal_ble_ops_t *g_ble_ops;

int hal_ble_register(const hal_ble_ops_t *ops)
{
    if (ops == NULL || ops->init == NULL) {
        return HAL_EINVAL;
    }
    if (g_ble_ops != NULL) {
        return HAL_EBUSY;
    }
    g_ble_ops = ops;
    return HAL_OK;
}

int hal_ble_init(void)
{
    if (g_ble_ops == NULL || g_ble_ops->init == NULL) {
        return HAL_ENODEV;
    }
    return g_ble_ops->init();
}

int hal_ble_start(void)
{
    if (g_ble_ops == NULL || g_ble_ops->start == NULL) {
        return HAL_ENODEV;
    }
    return g_ble_ops->start();
}

int hal_ble_stop(void)
{
    if (g_ble_ops == NULL || g_ble_ops->stop == NULL) {
        return HAL_ENODEV;
    }
    return g_ble_ops->stop();
}

int hal_ble_send(const void *buf, size_t len, int timeout_ms)
{
    if (g_ble_ops == NULL || g_ble_ops->send == NULL) {
        return HAL_ENODEV;
    }
    return g_ble_ops->send(buf, len, timeout_ms);
}

int hal_ble_recv(void *buf, size_t len, int timeout_ms)
{
    if (g_ble_ops == NULL || g_ble_ops->recv == NULL) {
        return HAL_ENODEV;
    }
    return g_ble_ops->recv(buf, len, timeout_ms);
}

int hal_ble_is_ready(void)
{
    if (g_ble_ops == NULL || g_ble_ops->is_ready == NULL) {
        return 0;
    }
    return g_ble_ops->is_ready();
}

int hal_ble_get_mtu(void)
{
    if (g_ble_ops == NULL || g_ble_ops->get_mtu == NULL) {
        return 0;
    }
    return g_ble_ops->get_mtu();
}

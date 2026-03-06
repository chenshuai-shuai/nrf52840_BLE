#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "spi_bus_arbiter.h"

LOG_MODULE_REGISTER(spi_bus_arbiter, LOG_LEVEL_WRN);

static struct k_mutex g_spi_bus_lock;
static bool g_spi_bus_inited;
static spi_bus_client_t g_spi_bus_owner = SPI_BUS_CLIENT_OTHER;
static uint32_t g_lock_ok_cnt;
static uint32_t g_lock_fail_cnt;
static int64_t g_last_log_ms;

int spi_bus_arbiter_init(void)
{
    if (g_spi_bus_inited) {
        return 0;
    }
    k_mutex_init(&g_spi_bus_lock);
    g_spi_bus_owner = SPI_BUS_CLIENT_OTHER;
    g_spi_bus_inited = true;
    return 0;
}

int spi_bus_lock(spi_bus_client_t client, k_timeout_t timeout)
{
    if (!g_spi_bus_inited) {
        (void)spi_bus_arbiter_init();
    }
    int ret = k_mutex_lock(&g_spi_bus_lock, timeout);
    if (ret == 0) {
        g_spi_bus_owner = client;
        g_lock_ok_cnt++;
    } else {
        g_lock_fail_cnt++;
    }

    int64_t now = k_uptime_get();
    if ((now - g_last_log_ms) >= 5000) {
        g_lock_ok_cnt = 0;
        g_lock_fail_cnt = 0;
        g_last_log_ms = now;
    }
    return ret;
}

int spi_bus_unlock(spi_bus_client_t client)
{
    if (!g_spi_bus_inited) {
        return -EINVAL;
    }
    if (g_spi_bus_owner != client) {
        LOG_WRN("spi bus unlock by non-owner: owner=%d req=%d",
                (int)g_spi_bus_owner, (int)client);
    }
    g_spi_bus_owner = SPI_BUS_CLIENT_OTHER;
    return k_mutex_unlock(&g_spi_bus_lock);
}

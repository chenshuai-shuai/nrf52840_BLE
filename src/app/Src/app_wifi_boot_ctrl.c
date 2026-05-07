#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_audio_route.h"
#include "app_esp_link.h"
#include "app_wifi_boot_ctrl.h"
#include "error.h"
#include "platform_shared_bus.h"

LOG_MODULE_REGISTER(app_wifi_boot_ctrl, LOG_LEVEL_INF);

#define WIFI_BOOT_ASSERT_MS     10
#define WIFI_BOOT_RELEASE_MS    120

static bool g_wifi_boot_ctrl_prepared;
static bool g_wifi_boot_ctrl_started;

static int wifi_boot_ctrl_prepare_mode(const char *op_name)
{
    int ret = app_audio_route_acquire_bootctrl();
    if (ret != HAL_OK) {
        LOG_ERR("wifi boot: acquire bootctrl failed for %s: %d", op_name, ret);
        return ret;
    }

    /* Re-apply the shared-bus GPIO mode before touching BOOT/EN so this
     * path remains correct even after prior audio ownership changes.
     */
    ret = platform_shared_bus_enter_nrf_bootctrl();
    if (ret != HAL_OK) {
        LOG_ERR("wifi boot: shared bus bootctrl apply failed for %s: %d", op_name, ret);
        return ret;
    }

    if (platform_shared_bus_get_mode() != PLATFORM_SHARED_BUS_NRF_BOOTCTRL) {
        LOG_ERR("wifi boot: shared bus mode mismatch before %s", op_name);
        return HAL_EIO;
    }

    LOG_INF("wifi boot: bootctrl mode ready for %s", op_name);
    return HAL_OK;
}

static int wifi_boot_ctrl_set(bool asserted)
{
    LOG_INF("wifi boot: set boot ctrl asserted=%d", asserted ? 1 : 0);
    return platform_shared_bus_set_boot_signal(asserted);
}

int app_wifi_boot_ctrl_prepare(void)
{
    int ret;

    if (g_wifi_boot_ctrl_prepared) {
        return HAL_OK;
    }

    ret = platform_shared_bus_init();
    if (ret != HAL_OK) {
        LOG_ERR("wifi boot: shared bus init failed: %d", ret);
        return ret;
    }

    g_wifi_boot_ctrl_prepared = true;
    LOG_INF("wifi boot: control pins prepared");
    return HAL_OK;
}

int app_wifi_boot_ctrl_enter_download(void)
{
    int ret = wifi_boot_ctrl_prepare_mode("enter_download");
    if (ret != HAL_OK) {
        return ret;
    }

    LOG_INF("wifi boot: enter download sequence");
    (void)app_esp_link_enter_passthrough();

    ret = wifi_boot_ctrl_set(true);
    if (ret != HAL_OK) {
        return ret;
    }
    k_msleep(WIFI_BOOT_ASSERT_MS);
    k_msleep(WIFI_BOOT_RELEASE_MS);
    LOG_WRN("wifi boot: download strap asserted on boot pin only; manual ESP reset required");
    return HAL_OK;
}

int app_wifi_boot_ctrl_boot_normal(void)
{
    int ret = wifi_boot_ctrl_prepare_mode("boot_normal");
    if (ret != HAL_OK) {
        return ret;
    }

    LOG_INF("wifi boot: enter normal boot sequence");
    (void)app_esp_link_enter_passthrough();

    ret = wifi_boot_ctrl_set(false);
    if (ret != HAL_OK) {
        return ret;
    }
    k_msleep(WIFI_BOOT_ASSERT_MS);
    k_msleep(WIFI_BOOT_RELEASE_MS);
    (void)app_esp_link_enter_runtime();
    ret = app_audio_route_finish_bootctrl();
    if (ret != HAL_OK) {
        return ret;
    }
    LOG_INF("wifi boot: normal boot strap set on boot pin only");
    return HAL_OK;
}

int app_wifi_boot_ctrl_reset_only(void)
{
    int ret = wifi_boot_ctrl_prepare_mode("reset_only");
    if (ret != HAL_OK) {
        return ret;
    }

    LOG_WRN("wifi boot: reset_only no longer drives EN; manual ESP reset required");
    return HAL_OK;
}

int app_wifi_boot_ctrl_start(void)
{
    int ret;

    if (g_wifi_boot_ctrl_started) {
        return HAL_OK;
    }

    ret = app_wifi_boot_ctrl_prepare();
    if (ret != HAL_OK) {
        return ret;
    }

    g_wifi_boot_ctrl_started = true;
    LOG_INF("wifi boot: control started");
    return HAL_OK;
}

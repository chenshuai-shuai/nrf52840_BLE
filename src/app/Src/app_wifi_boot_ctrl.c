#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_audio_route.h"
#include "app_wifi_boot_ctrl.h"
#include "error.h"

LOG_MODULE_REGISTER(app_wifi_boot_ctrl, LOG_LEVEL_INF);

#define WIFI_BOOT_CTRL_NODE DT_ALIAS(wifi_boot_ctrl)
#define WIFI_EN_CTRL_NODE   DT_ALIAS(wifi_en_ctrl)

#if !DT_NODE_HAS_STATUS(WIFI_BOOT_CTRL_NODE, okay)
#error "wifi-boot-ctrl alias is missing"
#endif

#if !DT_NODE_HAS_STATUS(WIFI_EN_CTRL_NODE, okay)
#error "wifi-en-ctrl alias is missing"
#endif

#define WIFI_BOOT_ASSERT_MS     10
#define WIFI_RESET_PULSE_MS     80
#define WIFI_BOOT_RELEASE_MS    120

static const struct gpio_dt_spec g_boot_ctrl = GPIO_DT_SPEC_GET(WIFI_BOOT_CTRL_NODE, gpios);
static const struct gpio_dt_spec g_wifi_en = GPIO_DT_SPEC_GET(WIFI_EN_CTRL_NODE, gpios);

static bool g_wifi_boot_ctrl_prepared;
static bool g_wifi_boot_ctrl_started;

static int wifi_boot_gpio_claim(void)
{
    if (!gpio_is_ready_dt(&g_boot_ctrl) || !gpio_is_ready_dt(&g_wifi_en)) {
        return HAL_ENODEV;
    }

    int ret = gpio_pin_configure_dt(&g_boot_ctrl, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
        return ret;
    }

    ret = gpio_pin_configure_dt(&g_wifi_en, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
        return ret;
    }

    return HAL_OK;
}

static void wifi_boot_ctrl_set(bool asserted)
{
    (void)gpio_pin_set_dt(&g_boot_ctrl, asserted ? 1 : 0);
}

static void wifi_en_reset_pulse(void)
{
    (void)gpio_pin_set_dt(&g_wifi_en, 1);
    k_msleep(WIFI_RESET_PULSE_MS);
    (void)gpio_pin_set_dt(&g_wifi_en, 0);
}

int app_wifi_boot_ctrl_prepare(void)
{
    int ret;

    if (g_wifi_boot_ctrl_prepared) {
        return HAL_OK;
    }

    ret = wifi_boot_gpio_claim();
    if (ret != HAL_OK) {
        LOG_ERR("wifi boot: gpio init failed: %d", ret);
        return ret;
    }

    g_wifi_boot_ctrl_prepared = true;
    LOG_INF("wifi boot: control pins prepared");
    return HAL_OK;
}

int app_wifi_boot_ctrl_enter_download(void)
{
    int ret = app_audio_route_enter_bootctrl();
    if (ret != HAL_OK) {
        return ret;
    }

    ret = wifi_boot_gpio_claim();
    if (ret != HAL_OK) {
        return ret;
    }

    wifi_boot_ctrl_set(true);
    k_msleep(WIFI_BOOT_ASSERT_MS);
    wifi_en_reset_pulse();
    k_msleep(WIFI_BOOT_RELEASE_MS);
    LOG_INF("wifi boot: download mode asserted");
    return HAL_OK;
}

int app_wifi_boot_ctrl_boot_normal(void)
{
    int ret = app_audio_route_enter_bootctrl();
    if (ret != HAL_OK) {
        return ret;
    }

    ret = wifi_boot_gpio_claim();
    if (ret != HAL_OK) {
        return ret;
    }

    wifi_boot_ctrl_set(false);
    k_msleep(WIFI_BOOT_ASSERT_MS);
    wifi_en_reset_pulse();
    k_msleep(WIFI_BOOT_RELEASE_MS);
    LOG_INF("wifi boot: normal boot asserted");
    return HAL_OK;
}

int app_wifi_boot_ctrl_reset_only(void)
{
    int ret = app_audio_route_enter_bootctrl();
    if (ret != HAL_OK) {
        return ret;
    }

    ret = wifi_boot_gpio_claim();
    if (ret != HAL_OK) {
        return ret;
    }

    wifi_en_reset_pulse();
    k_msleep(WIFI_BOOT_RELEASE_MS);
    LOG_INF("wifi boot: reset pulse done");
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

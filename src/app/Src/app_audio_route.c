#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <stdbool.h>

#include "app_audio_route.h"
#include "app_rtc.h"
#include "error.h"

LOG_MODULE_REGISTER(app_audio_route, LOG_LEVEL_INF);

#define AUDIO_GPIO0_NODE DT_NODELABEL(gpio0)
#define AUDIO_GPIO1_NODE DT_NODELABEL(gpio1)

struct audio_shared_pin {
    const struct device *port;
    gpio_pin_t pin;
    const char *name;
};

static const struct audio_shared_pin g_audio_shared_pins[] = {
    { DEVICE_DT_GET(AUDIO_GPIO0_NODE), 26U, "MIC_CLK" },
    { DEVICE_DT_GET(AUDIO_GPIO1_NODE), 0U,  "MIC_DOUT" },
    { DEVICE_DT_GET(AUDIO_GPIO1_NODE), 2U,  "AMP_BCLK" },
    { DEVICE_DT_GET(AUDIO_GPIO1_NODE), 4U,  "AMP_LRCLK" },
    { DEVICE_DT_GET(AUDIO_GPIO1_NODE), 6U,  "AMP_DIN" },
};

static struct {
    bool inited;
    app_audio_owner_t owner;
} g_audio_route;

K_MUTEX_DEFINE(g_audio_route_lock);

static int app_audio_route_force_shared_pins_hiz_locked(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(g_audio_shared_pins); i++) {
        const struct audio_shared_pin *pin = &g_audio_shared_pins[i];

        if (!device_is_ready(pin->port)) {
            LOG_ERR("audio route: gpio not ready for %s", pin->name);
            return HAL_ENODEV;
        }

        int ret = gpio_pin_configure(pin->port, pin->pin, GPIO_INPUT);
        if (ret != 0) {
            LOG_ERR("audio route: %s -> hi-z failed: %d", pin->name, ret);
            return ret;
        }
    }

    return HAL_OK;
}

static int app_audio_route_ensure_init_locked(void)
{
    if (g_audio_route.inited) {
        return HAL_OK;
    }

    int ret = app_audio_route_force_shared_pins_hiz_locked();
    if (ret != HAL_OK) {
        return ret;
    }

    g_audio_route.owner = APP_AUDIO_OWNER_NONE;
    g_audio_route.inited = true;
    LOG_INF("audio route: shared pins parked in hi-z");
    return HAL_OK;
}

int app_audio_route_init(void)
{
    int ret;

    k_mutex_lock(&g_audio_route_lock, K_FOREVER);
    ret = app_audio_route_ensure_init_locked();
    k_mutex_unlock(&g_audio_route_lock);

    return ret;
}

int app_audio_route_request_wifi(void)
{
    int ret;

    k_mutex_lock(&g_audio_route_lock, K_FOREVER);

    ret = app_audio_route_ensure_init_locked();
    if (ret != HAL_OK) {
        k_mutex_unlock(&g_audio_route_lock);
        return ret;
    }

    if (g_audio_route.owner == APP_AUDIO_OWNER_WIFI) {
        k_mutex_unlock(&g_audio_route_lock);
        return HAL_OK;
    }

    ret = app_rtc_audio_suspend();
    if (ret != HAL_OK) {
        k_mutex_unlock(&g_audio_route_lock);
        return ret;
    }

    ret = app_audio_route_force_shared_pins_hiz_locked();
    if (ret != HAL_OK) {
        (void)app_rtc_audio_resume();
        k_mutex_unlock(&g_audio_route_lock);
        return ret;
    }

    g_audio_route.owner = APP_AUDIO_OWNER_WIFI;
    k_mutex_unlock(&g_audio_route_lock);

    LOG_INF("audio route: owner -> WIFI");
    return HAL_OK;
}

int app_audio_route_release_wifi(void)
{
    int ret;
    int resume_ret;

    k_mutex_lock(&g_audio_route_lock, K_FOREVER);

    ret = app_audio_route_ensure_init_locked();
    if (ret != HAL_OK) {
        k_mutex_unlock(&g_audio_route_lock);
        return ret;
    }

    ret = app_audio_route_force_shared_pins_hiz_locked();
    if (ret != HAL_OK) {
        k_mutex_unlock(&g_audio_route_lock);
        return ret;
    }

    g_audio_route.owner = APP_AUDIO_OWNER_NRF;
    resume_ret = app_rtc_audio_resume();
    k_mutex_unlock(&g_audio_route_lock);

    if (resume_ret != HAL_OK) {
        return resume_ret;
    }

    LOG_INF("audio route: owner -> NRF");
    return HAL_OK;
}

app_audio_owner_t app_audio_route_owner_get(void)
{
    app_audio_owner_t owner;

    k_mutex_lock(&g_audio_route_lock, K_FOREVER);
    owner = g_audio_route.owner;
    k_mutex_unlock(&g_audio_route_lock);

    return owner;
}

bool app_audio_route_is_wifi_active(void)
{
    return app_audio_route_owner_get() == APP_AUDIO_OWNER_WIFI;
}

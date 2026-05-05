#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <stdbool.h>

#include "app_audio_route.h"
#include "app_bus.h"
#include "app_esp_link.h"
#include "app_rtc.h"
#include "app_uplink_service.h"
#include "error.h"

LOG_MODULE_REGISTER(app_audio_route, LOG_LEVEL_INF);

#define AUDIO_GPIO0_NODE DT_NODELABEL(gpio0)
#define AUDIO_GPIO1_NODE DT_NODELABEL(gpio1)

#define AUDIO_ROUTE_STACK_SIZE     1536
#define AUDIO_ROUTE_PRIO           5
#define AUDIO_ROUTE_GUARD_MS       10

struct audio_shared_pin {
    const struct device *port;
    gpio_pin_t pin;
    const char *name;
};

static const struct audio_shared_pin g_audio_shared_pins[] = {
    { DEVICE_DT_GET(AUDIO_GPIO0_NODE), 26U, "MIC_CLK"  },
    { DEVICE_DT_GET(AUDIO_GPIO1_NODE), 0U,  "MIC_DOUT" },
    { DEVICE_DT_GET(AUDIO_GPIO1_NODE), 11U, "SD_MODE"  },
    { DEVICE_DT_GET(AUDIO_GPIO1_NODE), 6U,  "AMP_DIN"  },
    { DEVICE_DT_GET(AUDIO_GPIO1_NODE), 2U,  "AMP_BCLK" },
    { DEVICE_DT_GET(AUDIO_GPIO1_NODE), 4U,  "AMP_LRCLK"},
};

static struct {
    bool inited;
    bool thread_started;
    bool target_nrf_owner;
    app_audio_route_state_t state;
} g_audio_route = {
    .state = APP_AUDIO_STATE_SAFE_HANDOFF,
};

static struct k_thread g_audio_route_thread;
K_THREAD_STACK_DEFINE(g_audio_route_stack, AUDIO_ROUTE_STACK_SIZE);
K_MUTEX_DEFINE(g_audio_route_lock);
K_SEM_DEFINE(g_audio_route_sem, 0, 1);

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

static int app_audio_route_enter_safe_locked(void)
{
    int ret = app_rtc_audio_suspend();
    if (ret != HAL_OK) {
        return ret;
    }

    ret = app_audio_route_force_shared_pins_hiz_locked();
    if (ret != HAL_OK) {
        return ret;
    }

    g_audio_route.state = APP_AUDIO_STATE_SAFE_HANDOFF;
    return HAL_OK;
}

static int app_audio_route_enter_nrf_audio_locked(void)
{
    int ret = app_audio_route_force_shared_pins_hiz_locked();
    if (ret != HAL_OK) {
        return ret;
    }

    k_msleep(AUDIO_ROUTE_GUARD_MS);

    ret = app_rtc_audio_resume();
    if (ret != HAL_OK) {
        return ret;
    }

    g_audio_route.state = APP_AUDIO_STATE_NRF_AUDIO_OWNER;
    return HAL_OK;
}

static int app_audio_route_enter_esp_audio_locked(void)
{
    int ret = app_audio_route_enter_safe_locked();
    if (ret != HAL_OK) {
        return ret;
    }

    g_audio_route.state = APP_AUDIO_STATE_ESP_AUDIO_OWNER;
    return HAL_OK;
}

static int app_audio_route_ensure_init_locked(void)
{
    if (g_audio_route.inited) {
        return HAL_OK;
    }

    int ret = app_audio_route_enter_safe_locked();
    if (ret != HAL_OK) {
        return ret;
    }

    g_audio_route.inited = true;
    LOG_INF("audio route: initialized in SAFE_HANDOFF");
    return HAL_OK;
}

static bool audio_route_should_target_nrf(void)
{
    return app_uplink_service_is_ready();
}

static void audio_route_request_target(bool target_nrf_owner)
{
    k_mutex_lock(&g_audio_route_lock, K_FOREVER);
    g_audio_route.target_nrf_owner = target_nrf_owner;
    k_mutex_unlock(&g_audio_route_lock);
    k_sem_give(&g_audio_route_sem);
}

static void audio_route_event_cb(const app_event_t *evt, void *user)
{
    ARG_UNUSED(user);

    if (evt == NULL) {
        return;
    }

    if (evt->id == APP_EVT_UPLINK_STATE) {
        audio_route_request_target(evt->data.uplink.ready);
        return;
    }

    if (evt->id == APP_EVT_APP_LIFECYCLE &&
        evt->data.lifecycle.app_id == APP_LC_WIFI_CTRL &&
        evt->data.lifecycle.status.started) {
        audio_route_request_target(audio_route_should_target_nrf());
    }
}

static void audio_route_monitor_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        bool want_nrf;

        (void)k_sem_take(&g_audio_route_sem, K_FOREVER);

        k_mutex_lock(&g_audio_route_lock, K_FOREVER);
        want_nrf = g_audio_route.target_nrf_owner;
        k_mutex_unlock(&g_audio_route_lock);

        if (want_nrf) {
            app_audio_route_state_t state = app_audio_route_get_state();
            if (state != APP_AUDIO_STATE_NRF_AUDIO_OWNER) {
                (void)app_audio_route_request_nrf_audio();
            }
        } else if (app_esp_link_is_started()) {
            app_audio_route_state_t state = app_audio_route_get_state();
            if (state != APP_AUDIO_STATE_ESP_AUDIO_OWNER) {
                (void)app_audio_route_request_esp_audio();
            }
        }
    }
}

int app_audio_route_init(void)
{
    int ret;

    k_mutex_lock(&g_audio_route_lock, K_FOREVER);
    ret = app_audio_route_ensure_init_locked();
    if (ret == HAL_OK) {
        g_audio_route.target_nrf_owner = audio_route_should_target_nrf();
    }
    k_mutex_unlock(&g_audio_route_lock);
    if (ret != HAL_OK) {
        return ret;
    }

    (void)app_bus_subscribe(APP_EVT_UPLINK_STATE, audio_route_event_cb, NULL);
    (void)app_bus_subscribe(APP_EVT_APP_LIFECYCLE, audio_route_event_cb, NULL);

    if (!g_audio_route.thread_started) {
        k_thread_create(&g_audio_route_thread,
                        g_audio_route_stack,
                        K_THREAD_STACK_SIZEOF(g_audio_route_stack),
                        audio_route_monitor_entry,
                        NULL, NULL, NULL,
                        AUDIO_ROUTE_PRIO,
                        0,
                        K_NO_WAIT);
        k_thread_name_set(&g_audio_route_thread, "audio_route");
        g_audio_route.thread_started = true;
    }

    k_sem_give(&g_audio_route_sem);

    return HAL_OK;
}

app_audio_route_state_t app_audio_route_get_state(void)
{
    app_audio_route_state_t state;

    k_mutex_lock(&g_audio_route_lock, K_FOREVER);
    state = g_audio_route.state;
    k_mutex_unlock(&g_audio_route_lock);

    return state;
}

int app_audio_route_enter_safe(void)
{
    int ret;

    k_mutex_lock(&g_audio_route_lock, K_FOREVER);
    ret = app_audio_route_ensure_init_locked();
    if (ret == HAL_OK) {
        ret = app_audio_route_enter_safe_locked();
    }
    k_mutex_unlock(&g_audio_route_lock);

    return ret;
}

int app_audio_route_enter_bootctrl(void)
{
    int ret;

    k_mutex_lock(&g_audio_route_lock, K_FOREVER);
    ret = app_audio_route_ensure_init_locked();
    if (ret == HAL_OK) {
        ret = app_audio_route_enter_safe_locked();
        if (ret == HAL_OK) {
            g_audio_route.state = APP_AUDIO_STATE_NRF_ESP_BOOTCTRL;
        }
    }
    k_mutex_unlock(&g_audio_route_lock);

    if (ret == HAL_OK) {
        LOG_INF("audio route: owner -> NRF_ESP_BOOTCTRL");
    }
    return ret;
}

int app_audio_route_request_nrf_audio(void)
{
    int ret;

    if (app_audio_route_get_state() == APP_AUDIO_STATE_NRF_AUDIO_OWNER) {
        return HAL_OK;
    }

    if (app_esp_link_is_started()) {
        ret = app_esp_link_request_audio_release();
        if (ret != HAL_OK) {
            LOG_WRN("audio route: esp release request failed: %d", ret);
            return ret;
        }
    }

    k_mutex_lock(&g_audio_route_lock, K_FOREVER);
    ret = app_audio_route_ensure_init_locked();
    if (ret == HAL_OK) {
        ret = app_audio_route_enter_safe_locked();
    }
    if (ret == HAL_OK) {
        ret = app_audio_route_enter_nrf_audio_locked();
    }
    k_mutex_unlock(&g_audio_route_lock);

    if (ret == HAL_OK) {
        LOG_INF("audio route: owner -> NRF_AUDIO_OWNER");
    }
    return ret;
}

int app_audio_route_request_esp_audio(void)
{
    int ret = app_audio_route_enter_safe();
    if (ret != HAL_OK) {
        return ret;
    }

    if (!app_esp_link_is_started()) {
        return HAL_ENODEV;
    }

    ret = app_esp_link_request_audio_take();
    if (ret != HAL_OK) {
        LOG_WRN("audio route: esp take request failed: %d", ret);
        return ret;
    }

    k_mutex_lock(&g_audio_route_lock, K_FOREVER);
    ret = app_audio_route_enter_esp_audio_locked();
    k_mutex_unlock(&g_audio_route_lock);

    if (ret == HAL_OK) {
        LOG_INF("audio route: owner -> ESP_AUDIO_OWNER");
    }
    return ret;
}

bool app_audio_route_is_nrf_owner(void)
{
    return app_audio_route_get_state() == APP_AUDIO_STATE_NRF_AUDIO_OWNER;
}

bool app_audio_route_is_esp_owner(void)
{
    return app_audio_route_get_state() == APP_AUDIO_STATE_ESP_AUDIO_OWNER;
}

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <stdbool.h>

#include "app_audio_route.h"
#include "app_bus.h"
#include "app_esp_link.h"
#include "app_rtc.h"
#include "app_uplink_service.h"
#include "error.h"
#include "platform_shared_bus.h"

LOG_MODULE_REGISTER(app_audio_route, LOG_LEVEL_INF);

#define AUDIO_ROUTE_STACK_SIZE     1536
#define AUDIO_ROUTE_PRIO           5
#define AUDIO_ROUTE_GUARD_MS       10

static struct {
    bool inited;
    bool thread_started;
    bool target_nrf_owner;
    bool ble_uplink_ready;
    bool ble_session_seen;
    app_audio_route_state_t state;
} g_audio_route = {
    .state = APP_AUDIO_STATE_SAFE_HANDOFF,
};

static struct k_thread g_audio_route_thread;
K_THREAD_STACK_DEFINE(g_audio_route_stack, AUDIO_ROUTE_STACK_SIZE);
K_MUTEX_DEFINE(g_audio_route_lock);
K_SEM_DEFINE(g_audio_route_sem, 0, 1);

static const char *audio_route_state_str(app_audio_route_state_t state)
{
    switch (state) {
    case APP_AUDIO_STATE_SAFE_HANDOFF:
        return "SAFE_HANDOFF";
    case APP_AUDIO_STATE_NRF_AUDIO_OWNER:
        return "NRF_AUDIO_OWNER";
    case APP_AUDIO_STATE_ESP_AUDIO_OWNER:
        return "ESP_AUDIO_OWNER";
    case APP_AUDIO_STATE_NRF_ESP_BOOTCTRL:
        return "NRF_ESP_BOOTCTRL";
    default:
        return "UNKNOWN";
    }
}

static int app_audio_route_enter_safe_locked(void)
{
    int ret = app_rtc_audio_suspend();
    if (ret != HAL_OK) {
        return ret;
    }

    ret = platform_shared_bus_enter_safe_handoff();
    if (ret != HAL_OK) {
        return ret;
    }

    g_audio_route.state = APP_AUDIO_STATE_SAFE_HANDOFF;
    return HAL_OK;
}

static int app_audio_route_enter_nrf_audio_locked(void)
{
    int ret = platform_shared_bus_enter_nrf_audio();
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

static bool audio_route_should_target_nrf_locked(void)
{
    /* Default audio owner policy:
     * - On boot, nRF owns audio by default.
     * - After BLE has been used at least once, a disconnected BLE uplink
     *   hands audio ownership over to ESP32 until BLE comes back.
     */
    return g_audio_route.ble_uplink_ready || !g_audio_route.ble_session_seen;
}

static bool audio_route_ble_connected_locked(void)
{
    return g_audio_route.ble_uplink_ready;
}

static void audio_route_request_target(bool target_nrf_owner)
{
    k_mutex_lock(&g_audio_route_lock, K_FOREVER);
    g_audio_route.target_nrf_owner = target_nrf_owner;
    LOG_INF("audio route: target -> %s (state=%s ble_ready=%d session_seen=%d)",
            target_nrf_owner ? "NRF" : "ESP",
            audio_route_state_str(g_audio_route.state),
            g_audio_route.ble_uplink_ready ? 1 : 0,
            g_audio_route.ble_session_seen ? 1 : 0);
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
        k_mutex_lock(&g_audio_route_lock, K_FOREVER);
        g_audio_route.ble_uplink_ready = evt->data.uplink.ready;
        if (evt->data.uplink.ready) {
            g_audio_route.ble_session_seen = true;
        }
        bool target_nrf_owner = audio_route_should_target_nrf_locked();
        LOG_INF("audio route: uplink ready=%d session_seen=%d current=%s target=%s",
                g_audio_route.ble_uplink_ready ? 1 : 0,
                g_audio_route.ble_session_seen ? 1 : 0,
                audio_route_state_str(g_audio_route.state),
                target_nrf_owner ? "NRF" : "ESP");
        k_mutex_unlock(&g_audio_route_lock);

        audio_route_request_target(target_nrf_owner);
        return;
    }

    if (evt->id == APP_EVT_ESP_LINK_STATE) {
        k_mutex_lock(&g_audio_route_lock, K_FOREVER);
        bool target_nrf_owner = audio_route_should_target_nrf_locked();
        k_mutex_unlock(&g_audio_route_lock);

        audio_route_request_target(target_nrf_owner);
        return;
    }

    if (evt->id == APP_EVT_APP_LIFECYCLE &&
        evt->data.lifecycle.app_id == APP_LC_WIFI_CTRL &&
        evt->data.lifecycle.status.started) {
        k_mutex_lock(&g_audio_route_lock, K_FOREVER);
        bool target_nrf_owner = audio_route_should_target_nrf_locked();
        k_mutex_unlock(&g_audio_route_lock);

        audio_route_request_target(target_nrf_owner);
    }
}

static void audio_route_monitor_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        bool want_nrf;
        app_audio_route_state_t state;

        (void)k_sem_take(&g_audio_route_sem, K_FOREVER);

        k_mutex_lock(&g_audio_route_lock, K_FOREVER);
        want_nrf = g_audio_route.target_nrf_owner;
        state = g_audio_route.state;
        k_mutex_unlock(&g_audio_route_lock);

        if (state == APP_AUDIO_STATE_NRF_ESP_BOOTCTRL) {
            LOG_INF("audio route: auto owner switch deferred in NRF_ESP_BOOTCTRL");
            continue;
        }

        if (want_nrf) {
            if (state != APP_AUDIO_STATE_NRF_AUDIO_OWNER) {
                (void)app_audio_route_request_nrf_audio();
            }
        } else {
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
        g_audio_route.target_nrf_owner = audio_route_should_target_nrf_locked();
    }
    k_mutex_unlock(&g_audio_route_lock);
    if (ret != HAL_OK) {
        return ret;
    }

    (void)app_bus_subscribe(APP_EVT_UPLINK_STATE, audio_route_event_cb, NULL);
    (void)app_bus_subscribe(APP_EVT_ESP_LINK_STATE, audio_route_event_cb, NULL);
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
    return app_audio_route_acquire_bootctrl();
}

int app_audio_route_acquire_bootctrl(void)
{
    int ret;

    k_mutex_lock(&g_audio_route_lock, K_FOREVER);
    ret = app_audio_route_ensure_init_locked();
    if (ret == HAL_OK && g_audio_route.state == APP_AUDIO_STATE_NRF_ESP_BOOTCTRL) {
        k_mutex_unlock(&g_audio_route_lock);
        LOG_INF("audio route: bootctrl already acquired");
        return HAL_OK;
    }
    if (ret == HAL_OK) {
        ret = app_audio_route_enter_safe_locked();
        if (ret == HAL_OK) {
            ret = platform_shared_bus_enter_nrf_bootctrl();
        }
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

int app_audio_route_finish_bootctrl(void)
{
    int ret;
    bool ble_connected;

    k_mutex_lock(&g_audio_route_lock, K_FOREVER);
    ret = app_audio_route_ensure_init_locked();
    if (ret == HAL_OK && g_audio_route.state != APP_AUDIO_STATE_NRF_ESP_BOOTCTRL) {
        k_mutex_unlock(&g_audio_route_lock);
        return HAL_OK;
    }

    ble_connected = audio_route_ble_connected_locked();
    if (ret == HAL_OK) {
        ret = app_audio_route_enter_safe_locked();
    }
    if (ret == HAL_OK && ble_connected) {
        ret = app_audio_route_enter_nrf_audio_locked();
    }
    if (ret == HAL_OK) {
        g_audio_route.target_nrf_owner = audio_route_should_target_nrf_locked();
    }
    k_mutex_unlock(&g_audio_route_lock);

    if (ret != HAL_OK) {
        return ret;
    }

    if (ble_connected) {
        LOG_INF("audio route: bootctrl exit -> NRF_AUDIO_OWNER (BLE connected)");
    } else {
        LOG_INF("audio route: bootctrl exit -> SAFE_HANDOFF (BLE disconnected)");
    }
    return HAL_OK;
}

int app_audio_route_force_nrf_audio(void)
{
    int ret;

    k_mutex_lock(&g_audio_route_lock, K_FOREVER);
    ret = app_audio_route_ensure_init_locked();
    if (ret == HAL_OK) {
        ret = app_audio_route_enter_safe_locked();
    }
    if (ret == HAL_OK) {
        ret = app_audio_route_enter_nrf_audio_locked();
        if (ret == HAL_OK) {
            g_audio_route.target_nrf_owner = true;
        }
    }
    k_mutex_unlock(&g_audio_route_lock);

    if (ret == HAL_OK) {
        LOG_INF("audio route: owner -> NRF_AUDIO_OWNER");
    }
    return ret;
}

int app_audio_route_request_nrf_audio(void)
{
    int ret;

    if (app_audio_route_get_state() == APP_AUDIO_STATE_NRF_AUDIO_OWNER) {
        return HAL_OK;
    }

    LOG_INF("audio route: handoff request -> NRF (from %s)",
            audio_route_state_str(app_audio_route_get_state()));

    if (app_esp_link_is_started()) {
        if (!app_esp_link_protocol_ready()) {
            LOG_WRN("audio route: esp protocol not ready for NRF handoff");
            return HAL_EBUSY;
        }
        LOG_INF("audio route: asking ESP to release audio");
        ret = app_esp_link_request_audio_release();
        if (ret != HAL_OK) {
            LOG_WRN("audio route: esp release request failed: %d", ret);
            return ret;
        }
    }

    k_mutex_lock(&g_audio_route_lock, K_FOREVER);
    ret = app_audio_route_ensure_init_locked();
    if (ret == HAL_OK) {
        LOG_INF("audio route: local enter SAFE_HANDOFF before NRF audio");
        ret = app_audio_route_enter_safe_locked();
    }
    if (ret == HAL_OK) {
        LOG_INF("audio route: local acquire NRF audio pins");
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
    LOG_INF("audio route: handoff request -> ESP (from %s)",
            audio_route_state_str(app_audio_route_get_state()));

    LOG_INF("audio route: local enter SAFE_HANDOFF before ESP handoff");
    int ret = app_audio_route_enter_safe();
    if (ret != HAL_OK) {
        return ret;
    }

    if (!app_esp_link_is_started()) {
        LOG_WRN("audio route: esp link not started for ESP handoff");
        return HAL_ENODEV;
    }
    if (!app_esp_link_protocol_ready()) {
        LOG_WRN("audio route: esp protocol not ready for ESP handoff");
        return HAL_EBUSY;
    }

    LOG_INF("audio route: asking ESP to take audio");
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

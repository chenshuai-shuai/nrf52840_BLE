#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "app_bus.h"
#include "error.h"
#include "rt_thread.h"

LOG_MODULE_REGISTER(app_bus, LOG_LEVEL_WRN);

#define APP_BUS_STACK_SIZE 2048
#define APP_BUS_PRIORITY   9
#define APP_BUS_QUEUE_LEN  16
#define APP_BUS_MAX_SUBS   8

typedef struct {
    app_event_cb_t cb;
    void *user;
} app_bus_sub_t;

typedef struct {
    bool started;
    struct k_thread thread;
    struct k_mutex lock;
    app_bus_sub_t subs[APP_EVT_COUNT][APP_BUS_MAX_SUBS];
} app_bus_ctx_t;

RT_THREAD_STACK_DEFINE(g_app_bus_stack, APP_BUS_STACK_SIZE);
K_MSGQ_DEFINE(g_app_bus_q, sizeof(app_event_t), APP_BUS_QUEUE_LEN, 4);
static app_bus_ctx_t g_bus;

static void app_bus_dispatch(const app_event_t *evt)
{
    app_bus_sub_t local[APP_BUS_MAX_SUBS];
    size_t cnt = 0;

    if (evt->id >= APP_EVT_COUNT) {
        return;
    }

    k_mutex_lock(&g_bus.lock, K_FOREVER);
    for (size_t i = 0; i < APP_BUS_MAX_SUBS; i++) {
        if (g_bus.subs[evt->id][i].cb != NULL) {
            local[cnt++] = g_bus.subs[evt->id][i];
        }
    }
    k_mutex_unlock(&g_bus.lock);

    for (size_t i = 0; i < cnt; i++) {
        local[i].cb(evt, local[i].user);
    }
}

static void app_bus_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        app_event_t evt;
        int ret = k_msgq_get(&g_app_bus_q, &evt, K_FOREVER);
        if (ret == 0) {
            app_bus_dispatch(&evt);
        }
    }
}

int app_bus_start(void)
{
    if (g_bus.started) {
        return HAL_OK;
    }

    memset(&g_bus, 0, sizeof(g_bus));
    k_mutex_init(&g_bus.lock);

    int ret = rt_thread_start(&g_bus.thread,
                              g_app_bus_stack,
                              K_THREAD_STACK_SIZEOF(g_app_bus_stack),
                              app_bus_thread_entry,
                              NULL, NULL, NULL,
                              APP_BUS_PRIORITY, 0,
                              "app_bus");
    if (ret != 0) {
        LOG_ERR("app bus: thread start failed: %d", ret);
        return HAL_EIO;
    }

    g_bus.started = true;
    LOG_INF("app bus started");
    return HAL_OK;
}

int app_bus_subscribe(app_event_id_t id, app_event_cb_t cb, void *user)
{
    if (id >= APP_EVT_COUNT || cb == NULL) {
        return HAL_EINVAL;
    }

    k_mutex_lock(&g_bus.lock, K_FOREVER);
    for (size_t i = 0; i < APP_BUS_MAX_SUBS; i++) {
        if (g_bus.subs[id][i].cb == NULL) {
            g_bus.subs[id][i].cb = cb;
            g_bus.subs[id][i].user = user;
            k_mutex_unlock(&g_bus.lock);
            return HAL_OK;
        }
    }
    k_mutex_unlock(&g_bus.lock);

    return HAL_EBUSY;
}

int app_bus_publish(const app_event_t *evt)
{
    if (evt == NULL || evt->id >= APP_EVT_COUNT) {
        return HAL_EINVAL;
    }

    int ret = k_msgq_put(&g_app_bus_q, evt, K_NO_WAIT);
    if (ret == 0) {
        return HAL_OK;
    }

    app_event_t drop_evt;
    if (k_msgq_get(&g_app_bus_q, &drop_evt, K_NO_WAIT) == 0) {
        LOG_WRN("app bus: queue full, dropped event id=%d", (int)drop_evt.id);
        ret = k_msgq_put(&g_app_bus_q, evt, K_NO_WAIT);
        if (ret == 0) {
            return HAL_OK;
        }
    }

    return HAL_EIO;
}

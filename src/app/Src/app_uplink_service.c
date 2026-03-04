#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>

#include "app_uplink_service.h"
#include "hal_ble.h"
#include "rt_thread.h"
#include "error.h"

LOG_MODULE_REGISTER(app_uplink, LOG_LEVEL_INF);

#define APP_UPLINK_STACK_SIZE 3072
#define APP_UPLINK_PRIORITY   6
#define APP_UPLINK_Q_LEN      128

typedef struct {
    app_data_ticket_t ticket;
    app_uplink_prio_t prio;
} app_uplink_item_t;

static struct k_thread g_uplink_thread;
RT_THREAD_STACK_DEFINE(g_uplink_stack, APP_UPLINK_STACK_SIZE);

K_MSGQ_DEFINE(g_uplink_q_high, sizeof(app_uplink_item_t), APP_UPLINK_Q_LEN, 4);
K_MSGQ_DEFINE(g_uplink_q_normal, sizeof(app_uplink_item_t), APP_UPLINK_Q_LEN, 4);
K_MSGQ_DEFINE(g_uplink_q_low, sizeof(app_uplink_item_t), APP_UPLINK_Q_LEN, 4);

static volatile bool g_uplink_started;
static volatile bool g_uplink_ready;

static struct k_msgq *queue_of_prio(app_uplink_prio_t prio)
{
    switch (prio) {
    case APP_UPLINK_PRIO_HIGH:
        return &g_uplink_q_high;
    case APP_UPLINK_PRIO_LOW:
        return &g_uplink_q_low;
    case APP_UPLINK_PRIO_NORMAL:
    default:
        return &g_uplink_q_normal;
    }
}

static int enqueue_item(app_uplink_prio_t prio, const app_uplink_item_t *item)
{
    struct k_msgq *q = queue_of_prio(prio);
    int ret = k_msgq_put(q, item, K_NO_WAIT);
    if (ret == 0) {
        return HAL_OK;
    }

    app_uplink_item_t drop;
    if (k_msgq_get(q, &drop, K_NO_WAIT) == 0) {
        ret = k_msgq_put(q, item, K_NO_WAIT);
        if (ret == 0) {
            return HAL_OK;
        }
    }
    return HAL_EIO;
}

static bool dequeue_item(app_uplink_item_t *out)
{
    if (k_msgq_get(&g_uplink_q_high, out, K_NO_WAIT) == 0) {
        return true;
    }
    if (k_msgq_get(&g_uplink_q_normal, out, K_NO_WAIT) == 0) {
        return true;
    }
    if (k_msgq_get(&g_uplink_q_low, out, K_MSEC(200)) == 0) {
        return true;
    }
    return false;
}

static void uplink_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    uint64_t last_stat_ms = k_uptime_get();
    uint32_t sent_ok = 0;
    uint32_t sent_drop = 0;

    while (1) {
        if (!g_uplink_ready) {
            int ret = hal_ble_init();
            if (ret == HAL_OK) {
                ret = hal_ble_start();
            }
            if (ret == HAL_OK) {
                g_uplink_ready = true;
                LOG_INF("uplink transport initialized");
            } else {
                k_msleep(500);
                continue;
            }
        }

        app_uplink_item_t item;
        if (!dequeue_item(&item)) {
            continue;
        }

        if (!hal_ble_is_ready()) {
            sent_drop++;
            k_msleep(10);
            continue;
        }

        app_data_record_t rec;
        int rret = app_data_store_read(&item.ticket, &rec);
        if (rret != HAL_OK) {
            sent_drop++;
            continue;
        }

        int mtu = hal_ble_get_mtu();
        if (mtu < 23) {
            mtu = 23;
        }
        if (rec.len > (uint16_t)(mtu - 3)) {
            sent_drop++;
            continue;
        }

        int sret = hal_ble_send(rec.data, rec.len, 0);
        if (sret == HAL_OK) {
            sent_ok++;
        } else if (sret == -EAGAIN || sret == -ENOMEM || sret == HAL_EBUSY) {
            (void)enqueue_item(item.prio, &item);
            k_msleep(5);
        } else {
            sent_drop++;
        }

        uint64_t now = k_uptime_get();
        if ((now - last_stat_ms) >= 5000U) {
            LOG_INF("uplink stat: ok=%u drop=%u ble_ready=%d mtu=%d",
                    (unsigned int)sent_ok,
                    (unsigned int)sent_drop,
                    hal_ble_is_ready(),
                    hal_ble_get_mtu());
            sent_ok = 0;
            sent_drop = 0;
            last_stat_ms = now;
        }
    }
}

int app_uplink_service_start(void)
{
    if (g_uplink_started) {
        return HAL_OK;
    }

    (void)app_data_store_init();
    k_msgq_purge(&g_uplink_q_high);
    k_msgq_purge(&g_uplink_q_normal);
    k_msgq_purge(&g_uplink_q_low);
    g_uplink_ready = false;

    int ret = rt_thread_start(&g_uplink_thread,
                              g_uplink_stack,
                              K_THREAD_STACK_SIZEOF(g_uplink_stack),
                              uplink_thread_entry,
                              NULL, NULL, NULL,
                              APP_UPLINK_PRIORITY, 0,
                              "uplink");
    if (ret != 0) {
        LOG_ERR("uplink thread start failed: %d", ret);
        return HAL_EIO;
    }

    g_uplink_started = true;
    LOG_INF("uplink service started");
    return HAL_OK;
}

int app_uplink_service_stop(void)
{
    return HAL_ENOTSUP;
}

bool app_uplink_service_is_ready(void)
{
    return g_uplink_ready && (hal_ble_is_ready() ? true : false);
}

int app_uplink_publish(app_data_part_t part,
                       app_uplink_prio_t prio,
                       const void *payload,
                       size_t len,
                       uint32_t ts_ms)
{
    if (payload == NULL || len == 0U) {
        return HAL_EINVAL;
    }

    if (!g_uplink_started || !g_uplink_ready || !hal_ble_is_ready()) {
        return HAL_EBUSY;
    }

    app_uplink_item_t item;
    int ret = app_data_store_put(part, payload, len, ts_ms, &item.ticket);
    if (ret != HAL_OK) {
        return ret;
    }
    item.prio = prio;

    return enqueue_item(prio, &item);
}

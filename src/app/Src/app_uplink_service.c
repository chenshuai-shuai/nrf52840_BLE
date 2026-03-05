#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>

#include "app_uplink_service.h"
#include "app_db.h"
#include "hal_ble.h"
#include "rt_thread.h"
#include "error.h"

LOG_MODULE_REGISTER(app_uplink, LOG_LEVEL_INF);

#define APP_UPLINK_STACK_SIZE 3072
#define APP_UPLINK_PRIORITY   6
#define APP_UPLINK_Q_LEN      128
#define APP_DOWNLINK_Q_LEN    128
#define APP_UPLINK_MIN_ATT_MTU 23
#define APP_UPLINK_ATT_HDR_LEN 3
#define APP_UPLINK_MAX_REC_LEN 256
#define APP_AUDIO_PKT_HDR_LEN 8
#define APP_AUDIO_PKT_MAGIC0  0xA5
#define APP_AUDIO_PKT_MAGIC1  0x5A
#define APP_UPLINK_BATCH_MAX  32

typedef struct {
    app_data_ticket_t ticket;
    app_uplink_prio_t prio;
} app_uplink_item_t;

typedef struct {
    app_data_ticket_t ticket;
} app_downlink_item_t;

static struct k_thread g_uplink_thread;
RT_THREAD_STACK_DEFINE(g_uplink_stack, APP_UPLINK_STACK_SIZE);

K_MSGQ_DEFINE(g_uplink_q_high, sizeof(app_uplink_item_t), APP_UPLINK_Q_LEN, 4);
K_MSGQ_DEFINE(g_uplink_q_normal, sizeof(app_uplink_item_t), APP_UPLINK_Q_LEN, 4);
K_MSGQ_DEFINE(g_uplink_q_low, sizeof(app_uplink_item_t), APP_UPLINK_Q_LEN, 4);
K_MSGQ_DEFINE(g_downlink_q, sizeof(app_downlink_item_t), APP_DOWNLINK_Q_LEN, 4);

static volatile bool g_uplink_started;
static volatile bool g_uplink_ready;
K_MUTEX_DEFINE(g_uplink_tx_lock);

static size_t uplink_max_payload_bytes(void)
{
    int mtu = hal_ble_get_mtu();
    if (mtu < APP_UPLINK_MIN_ATT_MTU) {
        mtu = APP_UPLINK_MIN_ATT_MTU;
    }

    size_t max_payload = (size_t)(mtu - APP_UPLINK_ATT_HDR_LEN);
    if (max_payload > APP_UPLINK_MAX_REC_LEN) {
        max_payload = APP_UPLINK_MAX_REC_LEN;
    }
    return max_payload;
}

static app_data_part_t classify_downlink_part(const uint8_t *buf, size_t len)
{
    if (buf != NULL &&
        len >= APP_AUDIO_PKT_HDR_LEN &&
        buf[0] == APP_AUDIO_PKT_MAGIC0 &&
        buf[1] == APP_AUDIO_PKT_MAGIC1) {
        return APP_DATA_PART_AUDIO_DOWN;
    }
    return APP_DATA_PART_CTRL;
}

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
    return HAL_EBUSY;
}

static int enqueue_downlink_item(const app_downlink_item_t *item)
{
    int ret = k_msgq_put(&g_downlink_q, item, K_NO_WAIT);
    if (ret == 0) {
        return HAL_OK;
    }

    app_downlink_item_t drop;
    if (k_msgq_get(&g_downlink_q, &drop, K_NO_WAIT) == 0) {
        ret = k_msgq_put(&g_downlink_q, item, K_NO_WAIT);
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
    if (k_msgq_get(&g_uplink_q_low, out, K_MSEC(20)) == 0) {
        return true;
    }
    return false;
}

static void poll_downlink_once(uint32_t *rx_ok, uint32_t *rx_drop)
{
    uint8_t buf[APP_UPLINK_MAX_REC_LEN];
    int rret = hal_ble_recv(buf, sizeof(buf), 0);
    if (rret <= 0) {
        return;
    }

    app_data_ticket_t ticket;
    app_data_part_t part = classify_downlink_part(buf, (size_t)rret);
    int pret = app_db_stream_put(part,
                                 buf,
                                 (size_t)rret,
                                 (uint32_t)k_uptime_get(),
                                 &ticket);
    if (pret != HAL_OK) {
        if (rx_drop != NULL) {
            (*rx_drop)++;
        }
        return;
    }

    app_downlink_item_t item = {
        .ticket = ticket,
    };
    if (enqueue_downlink_item(&item) == HAL_OK) {
        if (rx_ok != NULL) {
            (*rx_ok)++;
        }
    } else if (rx_drop != NULL) {
        (*rx_drop)++;
    }
}

static void uplink_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    uint64_t last_stat_ms = k_uptime_get();
    uint32_t sent_ok = 0;
    uint32_t sent_drop = 0;
    uint32_t recv_ok = 0;
    uint32_t recv_drop = 0;
    bool have_item = false;
    app_uplink_item_t item;
    uint8_t busy_retry = 0;

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

        for (int i = 0; i < 4; i++) {
            poll_downlink_once(&recv_ok, &recv_drop);
        }

        if (!have_item) {
            if (!dequeue_item(&item)) {
                continue;
            }
            have_item = true;
            busy_retry = 0;
        }

        if (!have_item) {
            continue;
        }

        if (!hal_ble_is_ready()) {
            sent_drop++;
            have_item = false;
            k_msleep(10);
            continue;
        }

        app_data_record_t rec;
        int rret = app_db_stream_read(&item.ticket, &rec);
        if (rret != HAL_OK) {
            sent_drop++;
            have_item = false;
            continue;
        }

        size_t max_payload = uplink_max_payload_bytes();
        if (rec.len > (uint16_t)max_payload) {
            sent_drop++;
            have_item = false;
            continue;
        }

        int sret = hal_ble_send(rec.data, rec.len, 0);
        if (sret == HAL_OK) {
            sent_ok++;
            have_item = false;
            busy_retry = 0;
        } else if (sret == -EAGAIN || sret == -ENOMEM || sret == HAL_EBUSY) {
            /* Keep current item and retry in place to preserve fragment order. */
            busy_retry++;
            if (busy_retry > 20U) {
                sent_drop++;
                have_item = false;
                busy_retry = 0;
            } else {
                k_msleep(2);
            }
        } else {
            sent_drop++;
            have_item = false;
            busy_retry = 0;
        }

        uint64_t now = k_uptime_get();
        if ((now - last_stat_ms) >= 5000U) {
            LOG_INF("uplink stat: tx_ok=%u tx_drop=%u rx_ok=%u rx_drop=%u ble_ready=%d mtu=%d",
                    (unsigned int)sent_ok,
                    (unsigned int)sent_drop,
                    (unsigned int)recv_ok,
                    (unsigned int)recv_drop,
                    hal_ble_is_ready(),
                    hal_ble_get_mtu());
            sent_ok = 0;
            sent_drop = 0;
            recv_ok = 0;
            recv_drop = 0;
            last_stat_ms = now;
        }
    }
}

int app_uplink_service_start(void)
{
    if (g_uplink_started) {
        return HAL_OK;
    }

    (void)app_db_init();
    k_msgq_purge(&g_uplink_q_high);
    k_msgq_purge(&g_uplink_q_normal);
    k_msgq_purge(&g_uplink_q_low);
    k_msgq_purge(&g_downlink_q);
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

size_t app_uplink_max_payload(void)
{
    return uplink_max_payload_bytes();
}

int app_uplink_take_downlink(app_data_record_t *out, int timeout_ms)
{
    if (out == NULL) {
        return HAL_EINVAL;
    }

    app_downlink_item_t item;
    k_timeout_t timeout = (timeout_ms < 0) ? K_FOREVER : K_MSEC(timeout_ms);
    int ret = k_msgq_get(&g_downlink_q, &item, timeout);
    if (ret != 0) {
        return ret;
    }

    return app_db_stream_read(&item.ticket, out);
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
    if (len > app_uplink_max_payload()) {
        return HAL_EINVAL;
    }

    if (!g_uplink_started || !g_uplink_ready || !hal_ble_is_ready()) {
        return HAL_EBUSY;
    }

    app_uplink_item_t item;
    int ret = app_db_stream_put(part, payload, len, ts_ms, &item.ticket);
    if (ret != HAL_OK) {
        return ret;
    }
    item.prio = prio;
    k_mutex_lock(&g_uplink_tx_lock, K_FOREVER);
    ret = enqueue_item(prio, &item);
    k_mutex_unlock(&g_uplink_tx_lock);
    return ret;
}

int app_uplink_publish_batch(app_data_part_t part,
                             app_uplink_prio_t prio,
                             const uint8_t *const *payloads,
                             const uint16_t *lens,
                             size_t count,
                             uint32_t ts_ms)
{
    if (payloads == NULL || lens == NULL || count == 0U || count > APP_UPLINK_BATCH_MAX) {
        return HAL_EINVAL;
    }
    if (!g_uplink_started || !g_uplink_ready || !hal_ble_is_ready()) {
        return HAL_EBUSY;
    }

    struct k_msgq *q = queue_of_prio(prio);
    size_t max_payload = app_uplink_max_payload();
    app_uplink_item_t items[APP_UPLINK_BATCH_MAX];

    k_mutex_lock(&g_uplink_tx_lock, K_FOREVER);

    uint32_t free_slots = (uint32_t)k_msgq_num_free_get(q);
    if (free_slots < (uint32_t)count) {
        k_mutex_unlock(&g_uplink_tx_lock);
        return HAL_EBUSY;
    }

    for (size_t i = 0; i < count; i++) {
        if (payloads[i] == NULL || lens[i] == 0U || lens[i] > max_payload) {
            k_mutex_unlock(&g_uplink_tx_lock);
            return HAL_EINVAL;
        }

        items[i].prio = prio;
        int ret = app_db_stream_put(part, payloads[i], lens[i], ts_ms, &items[i].ticket);
        if (ret != HAL_OK) {
            k_mutex_unlock(&g_uplink_tx_lock);
            return ret;
        }
    }

    for (size_t i = 0; i < count; i++) {
        int ret = enqueue_item(prio, &items[i]);
        if (ret != HAL_OK) {
            k_mutex_unlock(&g_uplink_tx_lock);
            return ret;
        }
    }

    k_mutex_unlock(&g_uplink_tx_lock);
    return HAL_OK;
}

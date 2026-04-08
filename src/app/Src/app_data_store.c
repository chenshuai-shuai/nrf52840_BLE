#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_data_store.h"
#include "error.h"

LOG_MODULE_REGISTER(app_data_store, LOG_LEVEL_WRN);

#define APP_DATA_MAX_PAYLOAD 256
#define APP_DATA_CTRL_SLOTS 8
#define APP_DATA_AUDIO_UP_SLOTS 40
#define APP_DATA_AUDIO_DOWN_SLOTS 40
#define APP_DATA_PPG_SLOTS 12
#define APP_DATA_IMU_SLOTS 12
#define APP_DATA_PM_SLOTS 12
#define APP_DATA_ATTITUDE_SLOTS 12

typedef struct {
    uint32_t rec_id;
    uint32_t ts_ms;
    uint16_t len;
    uint8_t data[APP_DATA_MAX_PAYLOAD];
} app_data_slot_t;

typedef struct {
    app_data_slot_t *slots;
    uint16_t cap;
    uint16_t widx;
    uint32_t next_id;
} app_data_ring_t;

K_MUTEX_DEFINE(g_store_lock);

static app_data_slot_t g_ctrl_slots[APP_DATA_CTRL_SLOTS];
static app_data_slot_t g_audio_up_slots[APP_DATA_AUDIO_UP_SLOTS];
static app_data_slot_t g_audio_down_slots[APP_DATA_AUDIO_DOWN_SLOTS];
static app_data_slot_t g_ppg_slots[APP_DATA_PPG_SLOTS];
static app_data_slot_t g_imu_slots[APP_DATA_IMU_SLOTS];
static app_data_slot_t g_pm_slots[APP_DATA_PM_SLOTS];
static app_data_slot_t g_attitude_slots[APP_DATA_ATTITUDE_SLOTS];

static app_data_ring_t g_rings[APP_DATA_PART_COUNT];

static app_data_ring_t *get_ring(app_data_part_t part)
{
    if (part < 0 || part >= APP_DATA_PART_COUNT) {
        return NULL;
    }
    return &g_rings[part];
}

int app_data_store_init(void)
{
    k_mutex_lock(&g_store_lock, K_FOREVER);

    memset(g_ctrl_slots, 0, sizeof(g_ctrl_slots));
    memset(g_audio_up_slots, 0, sizeof(g_audio_up_slots));
    memset(g_audio_down_slots, 0, sizeof(g_audio_down_slots));
    memset(g_ppg_slots, 0, sizeof(g_ppg_slots));
    memset(g_imu_slots, 0, sizeof(g_imu_slots));
    memset(g_pm_slots, 0, sizeof(g_pm_slots));
    memset(g_attitude_slots, 0, sizeof(g_attitude_slots));

    g_rings[APP_DATA_PART_CTRL] = (app_data_ring_t){
        .slots = g_ctrl_slots, .cap = (uint16_t)(sizeof(g_ctrl_slots) / sizeof(g_ctrl_slots[0]))
    };
    g_rings[APP_DATA_PART_AUDIO_UP] = (app_data_ring_t){
        .slots = g_audio_up_slots, .cap = (uint16_t)(sizeof(g_audio_up_slots) / sizeof(g_audio_up_slots[0]))
    };
    g_rings[APP_DATA_PART_AUDIO_DOWN] = (app_data_ring_t){
        .slots = g_audio_down_slots, .cap = (uint16_t)(sizeof(g_audio_down_slots) / sizeof(g_audio_down_slots[0]))
    };
    g_rings[APP_DATA_PART_PPG] = (app_data_ring_t){
        .slots = g_ppg_slots, .cap = (uint16_t)(sizeof(g_ppg_slots) / sizeof(g_ppg_slots[0]))
    };
    g_rings[APP_DATA_PART_IMU] = (app_data_ring_t){
        .slots = g_imu_slots, .cap = (uint16_t)(sizeof(g_imu_slots) / sizeof(g_imu_slots[0]))
    };
    g_rings[APP_DATA_PART_PM] = (app_data_ring_t){
        .slots = g_pm_slots, .cap = (uint16_t)(sizeof(g_pm_slots) / sizeof(g_pm_slots[0]))
    };
    g_rings[APP_DATA_PART_ATTITUDE] = (app_data_ring_t){
        .slots = g_attitude_slots, .cap = (uint16_t)(sizeof(g_attitude_slots) / sizeof(g_attitude_slots[0]))
    };

    k_mutex_unlock(&g_store_lock);
    LOG_INF("data store initialized");
    return HAL_OK;
}

int app_data_store_put(app_data_part_t part,
                       const void *buf,
                       size_t len,
                       uint32_t ts_ms,
                       app_data_ticket_t *out_ticket)
{
    if (buf == NULL || out_ticket == NULL) {
        return HAL_EINVAL;
    }
    if (len == 0 || len > APP_DATA_MAX_PAYLOAD) {
        return HAL_EINVAL;
    }

    app_data_ring_t *r = get_ring(part);
    if (r == NULL || r->slots == NULL || r->cap == 0) {
        return HAL_EINVAL;
    }

    k_mutex_lock(&g_store_lock, K_FOREVER);

    uint16_t idx = r->widx;
    app_data_slot_t *s = &r->slots[idx];
    s->rec_id = ++r->next_id;
    s->ts_ms = ts_ms;
    s->len = (uint16_t)len;
    memcpy(s->data, buf, len);

    r->widx = (uint16_t)((r->widx + 1U) % r->cap);

    out_ticket->part = part;
    out_ticket->rec_id = s->rec_id;

    k_mutex_unlock(&g_store_lock);
    return HAL_OK;
}

int app_data_store_read(const app_data_ticket_t *ticket, app_data_record_t *out)
{
    if (ticket == NULL || out == NULL) {
        return HAL_EINVAL;
    }

    app_data_ring_t *r = get_ring(ticket->part);
    if (r == NULL || r->slots == NULL || r->cap == 0) {
        return HAL_EINVAL;
    }

    k_mutex_lock(&g_store_lock, K_FOREVER);

    bool found = false;
    for (uint16_t i = 0; i < r->cap; i++) {
        app_data_slot_t *s = &r->slots[i];
        if (s->rec_id == ticket->rec_id && s->len > 0U && s->len <= APP_DATA_MAX_PAYLOAD) {
            out->part = ticket->part;
            out->rec_id = s->rec_id;
            out->ts_ms = s->ts_ms;
            out->len = s->len;
            memcpy(out->data, s->data, s->len);
            found = true;
            break;
        }
    }

    k_mutex_unlock(&g_store_lock);
    return found ? HAL_OK : -ENOENT;
}

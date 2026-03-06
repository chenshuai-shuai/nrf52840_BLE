#include <string.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_db.h"
#include "error.h"

LOG_MODULE_REGISTER(app_db, LOG_LEVEL_WRN);

#define APP_DB_KV_CAP 32

typedef struct {
    bool used;
    app_db_key_t key;
    uint32_t ts_ms;
    uint16_t len;
    uint8_t value[APP_DB_KV_MAX_VALUE_LEN];
} app_db_kv_slot_t;

K_MUTEX_DEFINE(g_kv_lock);
static app_db_kv_slot_t g_kv_slots[APP_DB_KV_CAP];
static uint16_t g_kv_widx;
static bool g_db_inited;

static app_db_kv_slot_t *find_slot_by_key(app_db_key_t key)
{
    for (uint16_t i = 0; i < APP_DB_KV_CAP; i++) {
        if (g_kv_slots[i].used && g_kv_slots[i].key == key) {
            return &g_kv_slots[i];
        }
    }
    return NULL;
}

int app_db_init(void)
{
    if (g_db_inited) {
        return HAL_OK;
    }

    k_mutex_lock(&g_kv_lock, K_FOREVER);
    memset(g_kv_slots, 0, sizeof(g_kv_slots));
    g_kv_widx = 0;
    k_mutex_unlock(&g_kv_lock);

    (void)app_data_store_init();
    g_db_inited = true;
    LOG_INF("db initialized");
    return HAL_OK;
}

int app_db_kv_set(app_db_key_t key,
                  const void *value,
                  size_t len,
                  uint32_t ts_ms)
{
    if (value == NULL || len == 0U || len > APP_DB_KV_MAX_VALUE_LEN) {
        return HAL_EINVAL;
    }

    k_mutex_lock(&g_kv_lock, K_FOREVER);

    app_db_kv_slot_t *slot = find_slot_by_key(key);
    if (slot == NULL) {
        slot = &g_kv_slots[g_kv_widx];
        g_kv_widx = (uint16_t)((g_kv_widx + 1U) % APP_DB_KV_CAP);
    }

    slot->used = true;
    slot->key = key;
    slot->ts_ms = ts_ms;
    slot->len = (uint16_t)len;
    memcpy(slot->value, value, len);

    k_mutex_unlock(&g_kv_lock);
    return HAL_OK;
}

int app_db_kv_get(app_db_key_t key,
                  void *out_value,
                  size_t *inout_len,
                  uint32_t *out_ts_ms)
{
    if (out_value == NULL || inout_len == NULL || *inout_len == 0U) {
        return HAL_EINVAL;
    }

    k_mutex_lock(&g_kv_lock, K_FOREVER);
    app_db_kv_slot_t *slot = find_slot_by_key(key);
    if (slot == NULL) {
        k_mutex_unlock(&g_kv_lock);
        return HAL_ENODEV;
    }

    size_t copy_len = slot->len;
    if (copy_len > *inout_len) {
        copy_len = *inout_len;
    }
    memcpy(out_value, slot->value, copy_len);
    *inout_len = copy_len;
    if (out_ts_ms != NULL) {
        *out_ts_ms = slot->ts_ms;
    }
    k_mutex_unlock(&g_kv_lock);
    return HAL_OK;
}

int app_db_stream_put(app_data_part_t part,
                      const void *buf,
                      size_t len,
                      uint32_t ts_ms,
                      app_data_ticket_t *out_ticket)
{
    return app_data_store_put(part, buf, len, ts_ms, out_ticket);
}

int app_db_stream_read(const app_data_ticket_t *ticket, app_data_record_t *out)
{
    return app_data_store_read(ticket, out);
}

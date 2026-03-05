#ifndef APP_DB_H
#define APP_DB_H

#include <stddef.h>
#include <stdint.h>

#include "app_data_store.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_DB_KV_MAX_VALUE_LEN 64

typedef uint16_t app_db_key_t;

int app_db_init(void);

int app_db_kv_set(app_db_key_t key,
                  const void *value,
                  size_t len,
                  uint32_t ts_ms);

int app_db_kv_get(app_db_key_t key,
                  void *out_value,
                  size_t *inout_len,
                  uint32_t *out_ts_ms);

int app_db_stream_put(app_data_part_t part,
                      const void *buf,
                      size_t len,
                      uint32_t ts_ms,
                      app_data_ticket_t *out_ticket);

int app_db_stream_read(const app_data_ticket_t *ticket, app_data_record_t *out);

#ifdef __cplusplus
}
#endif

#endif /* APP_DB_H */

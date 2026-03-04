#ifndef APP_DATA_STORE_H
#define APP_DATA_STORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_DATA_PART_CTRL = 0,
    APP_DATA_PART_AUDIO_UP,
    APP_DATA_PART_AUDIO_DOWN,
    APP_DATA_PART_PPG,
    APP_DATA_PART_IMU,
    APP_DATA_PART_PM,
    APP_DATA_PART_COUNT
} app_data_part_t;

typedef struct {
    app_data_part_t part;
    uint32_t rec_id;
} app_data_ticket_t;

typedef struct {
    app_data_part_t part;
    uint32_t rec_id;
    uint32_t ts_ms;
    uint16_t len;
    uint8_t data[256];
} app_data_record_t;

int app_data_store_init(void);
int app_data_store_put(app_data_part_t part,
                       const void *buf,
                       size_t len,
                       uint32_t ts_ms,
                       app_data_ticket_t *out_ticket);
int app_data_store_read(const app_data_ticket_t *ticket, app_data_record_t *out);

#ifdef __cplusplus
}
#endif

#endif /* APP_DATA_STORE_H */

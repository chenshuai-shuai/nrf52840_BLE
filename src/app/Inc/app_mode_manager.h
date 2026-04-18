#ifndef APP_MODE_MANAGER_H
#define APP_MODE_MANAGER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_MODE_NORMAL = 0,
    APP_MODE_ESP_FW_UPDATE,
} app_mode_t;

void app_mode_manager_init(void);
app_mode_t app_mode_get(void);
bool app_mode_is_esp_fw_update(void);
int app_mode_enter_esp_fw_update(void);
int app_mode_exit_esp_fw_update(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MODE_MANAGER_H */

#ifndef APP_BLE_MAINT_CTRL_H
#define APP_BLE_MAINT_CTRL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int app_ble_maint_ctrl_init(void);
bool app_ble_maint_ctrl_handle_message(const uint8_t *buf, int len);

#ifdef __cplusplus
}
#endif

#endif /* APP_BLE_MAINT_CTRL_H */

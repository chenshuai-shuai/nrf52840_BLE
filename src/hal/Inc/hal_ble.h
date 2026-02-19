#ifndef HAL_BLE_H
#define HAL_BLE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int (*init)(void);
    int (*start)(void);
    int (*stop)(void);
    int (*send)(const void *buf, size_t len, int timeout_ms);
    int (*recv)(void *buf, size_t len, int timeout_ms);
    int (*is_ready)(void);
    int (*get_mtu)(void);
} hal_ble_ops_t;

int hal_ble_register(const hal_ble_ops_t *ops);
int hal_ble_init(void);
int hal_ble_start(void);
int hal_ble_stop(void);
int hal_ble_send(const void *buf, size_t len, int timeout_ms);
int hal_ble_recv(void *buf, size_t len, int timeout_ms);
int hal_ble_is_ready(void);
int hal_ble_get_mtu(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_BLE_H */

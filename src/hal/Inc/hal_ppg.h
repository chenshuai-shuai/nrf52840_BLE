#ifndef HAL_PPG_H
#define HAL_PPG_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int (*init)(void);
    int (*start)(void);
    int (*stop)(void);
    int (*read)(void *buf, size_t len, int timeout_ms);
} hal_ppg_ops_t;

int hal_ppg_register(const hal_ppg_ops_t *ops);
int hal_ppg_init(void);
int hal_ppg_start(void);
int hal_ppg_stop(void);
int hal_ppg_read(void *buf, size_t len, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* HAL_PPG_H */

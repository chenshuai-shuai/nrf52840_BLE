#ifndef HAL_SPK_H
#define HAL_SPK_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int (*init)(void);
    int (*start)(void);
    int (*stop)(void);
    int (*write)(const void *buf, size_t len, int timeout_ms);
} hal_spk_ops_t;

int hal_spk_register(const hal_spk_ops_t *ops);
int hal_spk_init(void);
int hal_spk_start(void);
int hal_spk_stop(void);
int hal_spk_write(const void *buf, size_t len, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SPK_H */

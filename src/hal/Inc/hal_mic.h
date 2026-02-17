#ifndef HAL_MIC_H
#define HAL_MIC_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int (*init)(void);
    int (*start)(void);
    int (*stop)(void);
    int (*read_block)(void **buf, size_t *len, int timeout_ms);
    int (*release)(void *buf);
} hal_mic_ops_t;

int hal_mic_register(const hal_mic_ops_t *ops);
int hal_mic_init(void);
int hal_mic_start(void);
int hal_mic_stop(void);
int hal_mic_read_block(void **buf, size_t *len, int timeout_ms);
int hal_mic_release(void *buf);

#ifdef __cplusplus
}
#endif

#endif /* HAL_MIC_H */

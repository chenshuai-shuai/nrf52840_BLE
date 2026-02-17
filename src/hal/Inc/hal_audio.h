#ifndef HAL_AUDIO_H
#define HAL_AUDIO_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_AUDIO_DIR_INPUT = 0,
    HAL_AUDIO_DIR_OUTPUT = 1,
} hal_audio_dir_t;

typedef struct {
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
} hal_audio_format_t;

typedef struct {
    hal_audio_format_t format;
    uint16_t frame_samples;
} hal_audio_cfg_t;

typedef struct {
    int (*init)(void);
    int (*start)(hal_audio_dir_t dir, const hal_audio_cfg_t *cfg);
    int (*stop)(hal_audio_dir_t dir);
    int (*read_block)(void **buf, size_t *len, int timeout_ms);
    int (*release)(void *buf);
    int (*read)(void *buf, size_t len, int timeout_ms);
    int (*write)(const void *buf, size_t len, int timeout_ms);
} hal_audio_ops_t;

int hal_audio_register(const hal_audio_ops_t *ops);
int hal_audio_init(void);
int hal_audio_start(hal_audio_dir_t dir, const hal_audio_cfg_t *cfg);
int hal_audio_stop(hal_audio_dir_t dir);
int hal_audio_read_block(void **buf, size_t *len, int timeout_ms);
int hal_audio_release(void *buf);
int hal_audio_read(void *buf, size_t len, int timeout_ms);
int hal_audio_write(const void *buf, size_t len, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* HAL_AUDIO_H */

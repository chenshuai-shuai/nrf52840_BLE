#ifndef HAL_FLASH_H
#define HAL_FLASH_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int (*init)(void);
    int (*read)(uint32_t addr, void *buf, size_t len);
    int (*write)(uint32_t addr, const void *buf, size_t len);
    int (*erase)(uint32_t addr, size_t len);
} hal_flash_ops_t;

int hal_flash_register(const hal_flash_ops_t *ops);
int hal_flash_init(void);
int hal_flash_read(uint32_t addr, void *buf, size_t len);
int hal_flash_write(uint32_t addr, const void *buf, size_t len);
int hal_flash_erase(uint32_t addr, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* HAL_FLASH_H */

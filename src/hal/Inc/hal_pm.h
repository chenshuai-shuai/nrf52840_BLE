#ifndef HAL_PM_H
#define HAL_PM_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int (*init)(void);
    int (*set_mode)(int mode);
    int (*get_status)(int *status);
} hal_pm_ops_t;

int hal_pm_register(const hal_pm_ops_t *ops);
int hal_pm_init(void);
int hal_pm_set_mode(int mode);
int hal_pm_get_status(int *status);

#ifdef __cplusplus
}
#endif

#endif /* HAL_PM_H */

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
    int (*set_gpio1)(int level);
} hal_pm_ops_t;

enum {
    HAL_PM_MODE_DEFAULT = 0,
    HAL_PM_MODE_RAILS_OFF = 1,
    HAL_PM_MODE_CHG_DISABLE = 2,
    HAL_PM_MODE_CHG_ENABLE = 3,
};

int hal_pm_register(const hal_pm_ops_t *ops);
int hal_pm_init(void);
int hal_pm_set_mode(int mode);
int hal_pm_get_status(int *status);
int hal_pm_set_gpio1(int level);

#ifdef __cplusplus
}
#endif

#endif /* HAL_PM_H */

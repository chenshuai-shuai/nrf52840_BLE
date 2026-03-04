#ifndef PM_SERVICE_H
#define PM_SERVICE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int pm_service_start(void);
bool pm_service_is_ready(void);
int pm_service_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_SERVICE_H */

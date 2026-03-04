#ifndef APP_LIFECYCLE_H
#define APP_LIFECYCLE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_LC_PM = 0,
    APP_LC_UPLINK,
    APP_LC_RTC,
    APP_LC_IMU,
    APP_LC_PPG,
    APP_LC_PM_TEST,
    APP_LC_COUNT
} app_lifecycle_id_t;

typedef struct {
    bool configured;
    bool started;
    bool ready;
    int last_error;
} app_lifecycle_status_t;

const char *app_lifecycle_name(app_lifecycle_id_t id);
bool app_lifecycle_is_enabled(app_lifecycle_id_t id);
int app_lifecycle_set_enabled(app_lifecycle_id_t id, bool enabled);
int app_lifecycle_start(app_lifecycle_id_t id);
int app_lifecycle_stop(app_lifecycle_id_t id);
int app_lifecycle_start_all(void);
int app_lifecycle_stop_all(void);
int app_lifecycle_get_status(app_lifecycle_id_t id, app_lifecycle_status_t *out);

#ifdef __cplusplus
}
#endif

#endif /* APP_LIFECYCLE_H */

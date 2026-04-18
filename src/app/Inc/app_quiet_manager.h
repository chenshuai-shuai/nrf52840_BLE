#ifndef APP_QUIET_MANAGER_H
#define APP_QUIET_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

void app_quiet_manager_init(void);
int app_quiet_manager_enter_update(void);
int app_quiet_manager_exit_update(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_QUIET_MANAGER_H */

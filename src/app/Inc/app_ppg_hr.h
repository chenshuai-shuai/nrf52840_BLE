#pragma once

#include "hal_ppg.h"

typedef enum {
    APP_PPG_HR_ST_NOT_READY = 0,
    APP_PPG_HR_ST_LOCKING = 1,
    APP_PPG_HR_ST_TRACKING = 2,
} app_ppg_hr_state_t;

int app_ppg_hr_start(void);
app_ppg_hr_state_t app_ppg_hr_get_state(void);
int app_ppg_hr_get_latest_sample(hal_ppg_sample_t *out);

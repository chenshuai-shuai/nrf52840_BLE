#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>

#include "app_ppg_hr.h"
#include "hal_ppg.h"
#include "error.h"
#include "rt_thread.h"
#include "system_state.h"
#include "app_bus.h"
#include "app_uplink_service.h"
#include "hal_imu.h"
#include "gh3x2x_demo_algo_call.h"
#include "goodix_hba.h"

LOG_MODULE_REGISTER(app_ppg_hr, LOG_LEVEL_INF);

#define APP_PPG_HR_STACK_SIZE 2304
#define APP_PPG_HR_PRIORITY   7
#define APP_PPG_HR_WARMUP_MS  3000U
#define APP_PPG_HR_MIN_BPM    40
#define APP_PPG_HR_MAX_BPM    220
#define APP_PPG_HR_ACQ_MIN_BPM 55
#define APP_PPG_HR_ACQ_MAX_BPM 140
#define APP_PPG_HR_CONF_SOFT  60
#define APP_PPG_HR_CONF_HARD  85
#define APP_PPG_HR_CONF_LOCK  78
#define APP_PPG_HR_CONF_TRACK_MIN 60
#define APP_PPG_HR_CONF_LOCK_RELAX 68
#define APP_PPG_HR_STABLE_N   3
#define APP_PPG_HR_STABLE_D   3
#define APP_PPG_HR_LOCK_BPM_MAX   45
#define APP_PPG_HR_LOCK_CONF_MIN  90
#define APP_PPG_HR_LOCK_HOLD_N    8
#define APP_PPG_HR_RECOVERY_GAP_MS 8000U
#define APP_PPG_HR_HIGH_LOCK_BPM_MIN   160
#define APP_PPG_HR_HIGH_LOCK_CONF_MAX  80
#define APP_PPG_HR_HIGH_LOCK_HOLD_N    8
#define APP_PPG_HR_NO_VALID_RECOVERY_MS 12000U
#define APP_PPG_HR_TRACK_NO_VALID_RECOVERY_MS 20000U
#define APP_PPG_HR_FAST_LOCK_MIN_MS    1500U
#define APP_PPG_HR_FAST_LOCK_CONF      92
#define APP_PPG_HR_FAST_LOCK_N         2U
#define APP_PPG_HR_RELAX_LOCK_MIN_MS   6000U
#define APP_PPG_HR_RELAX_LOCK_STABLE_N 4U
#define APP_PPG_HR_ACQ_RECOVERY_TIMEOUT_MS 45000U
#define APP_PPG_HR_ACQ_STUCK_LOW_BPM      42
#define APP_PPG_HR_ACQ_STUCK_CONF_MIN     95
#define APP_PPG_HR_ACQ_STUCK_HOLD_N       10
#define APP_PPG_HR_ACQ_STUCK_MIN_MS       15000U
#define APP_PPG_HR_ACQ_FIRST_RETRY_MS     0U
#define APP_PPG_LOG_PERIOD_MS             5000U
#define APP_PPG_HR_AGGR_LOCK_WINDOW_MS    12000U
#define APP_PPG_HR_AGGR_LOCK_CONF         72
#define APP_PPG_HR_AGGR_LOCK_STABLE_N     2U
#define APP_PPG_HR_SCENE_UPDATE_MS        1000U
#define APP_PPG_HR_SCENE_SWITCH_UP_N      3U
#define APP_PPG_HR_SCENE_SWITCH_DOWN_N    5U
#define APP_PPG_HR_MOTION_THR_UP          420
#define APP_PPG_HR_MOTION_THR_DOWN        220

typedef enum {
    APP_PPG_MODE_ACQUIRE = 0,
    APP_PPG_MODE_TRACK = 1,
} app_ppg_mode_t;

static struct k_thread g_ppg_hr_thread;
RT_THREAD_STACK_DEFINE(g_ppg_hr_stack, APP_PPG_HR_STACK_SIZE);
static struct k_mutex g_ppg_hr_state_lock;
static app_ppg_hr_state_t g_ppg_hr_state = APP_PPG_HR_ST_NOT_READY;
static hal_ppg_sample_t g_ppg_hr_latest = {0};
static bool g_ppg_hr_latest_valid;

static void ppg_hr_set_state(app_ppg_hr_state_t st)
{
    k_mutex_lock(&g_ppg_hr_state_lock, K_FOREVER);
    g_ppg_hr_state = st;
    k_mutex_unlock(&g_ppg_hr_state_lock);
}

static void ppg_hr_set_latest(const hal_ppg_sample_t *s)
{
    k_mutex_lock(&g_ppg_hr_state_lock, K_FOREVER);
    g_ppg_hr_latest = *s;
    g_ppg_hr_latest_valid = true;
    k_mutex_unlock(&g_ppg_hr_state_lock);
}

static bool hr_is_physically_valid(const hal_ppg_sample_t *s)
{
    return (s->hr_bpm >= APP_PPG_HR_MIN_BPM) && (s->hr_bpm <= APP_PPG_HR_MAX_BPM);
}

static int32_t i32_abs_diff(int32_t a, int32_t b)
{
    return (a >= b) ? (a - b) : (b - a);
}

static uint32_t ppg_motion_score_from_imu(const imu_sample_t *imu)
{
    int32_t ax = (int32_t)imu->accel_x;
    int32_t ay = (int32_t)imu->accel_y;
    int32_t az = (int32_t)imu->accel_z;
    int32_t gmag_l1 = i32_abs_diff(ax, 0) + i32_abs_diff(ay, 0) + i32_abs_diff(az, 0);
    int32_t score = i32_abs_diff(gmag_l1, 2048);
    return (score < 0) ? 0U : (uint32_t)score;
}

static void app_ppg_hr_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    uint32_t timeout_cnt = 0;
    int64_t last_timeout_log_ms = k_uptime_get();
    int64_t last_ppg_log_ms = k_uptime_get();
    int64_t last_uplink_log_ms = k_uptime_get();
    uint32_t valid_out_cnt = 0;
    uint32_t ppg_tx_ok = 0;
    uint32_t ppg_tx_drop = 0;
    int64_t start_ms = k_uptime_get();
    bool warmup_done_logged = false;
    app_ppg_mode_t mode = APP_PPG_MODE_ACQUIRE;
    uint32_t filtered_cnt = 0;
    uint32_t stable_cnt = 0;
    uint32_t low_lock_cnt = 0;
    uint32_t high_lock_cnt = 0;
    uint32_t acq_low_cnt = 0;
    uint32_t recover_cnt = 0;
    int32_t last_candidate_hr = 0;
    bool has_candidate = false;
    int64_t last_recover_ms = 0;
    int64_t last_valid_ms = start_ms;
    int64_t last_result_ms = start_ms;
    int64_t last_scene_eval_ms = start_ms;
    uint8_t cur_scene = (uint8_t)HBA_SCENES_STILL_REST;
    uint8_t motion_up_cnt = 0;
    uint8_t motion_down_cnt = 0;
    ppg_hr_set_state(APP_PPG_HR_ST_LOCKING);

    while (1) {
        hal_ppg_sample_t sample;
        int ret = hal_ppg_read(&sample, sizeof(sample), 2000);
        if (ret == HAL_OK) {
            int64_t now_ms = k_uptime_get();
            last_result_ms = now_ms;
#if IS_ENABLED(CONFIG_PPG_TUNE_MODE)
            LOG_INF("ppg raw: hr=%d conf=%d snr=%d frame=%u ts=%u",
                    (int)sample.hr_bpm,
                    (int)sample.confidence,
                    (int)sample.snr,
                    (unsigned int)sample.frame_id,
                    (unsigned int)sample.timestamp_ms);
#endif
            bool warmup_done = (uint32_t)(now_ms - start_ms) >= APP_PPG_HR_WARMUP_MS;
            if (warmup_done && !warmup_done_logged) {
                LOG_INF("ppg hr: warmup done, acquiring lock");
                warmup_done_logged = true;
            }

            if ((uint32_t)(now_ms - last_scene_eval_ms) >= APP_PPG_HR_SCENE_UPDATE_MS) {
                imu_sample_t imu = {0};
                uint32_t imu_ts = 0;
                if (hal_imu_get_latest(&imu, &imu_ts) == HAL_OK) {
                    ARG_UNUSED(imu_ts);
                    uint32_t motion = ppg_motion_score_from_imu(&imu);
                    if (motion >= APP_PPG_HR_MOTION_THR_UP) {
                        if (motion_up_cnt < 255U) {
                            motion_up_cnt++;
                        }
                        motion_down_cnt = 0;
                    } else if (motion <= APP_PPG_HR_MOTION_THR_DOWN) {
                        if (motion_down_cnt < 255U) {
                            motion_down_cnt++;
                        }
                        motion_up_cnt = 0;
                    } else {
                        motion_up_cnt = 0;
                        motion_down_cnt = 0;
                    }

                    if ((cur_scene == (uint8_t)HBA_SCENES_STILL_REST) &&
                        (motion_up_cnt >= APP_PPG_HR_SCENE_SWITCH_UP_N)) {
                        (void)GH3X2X_HbAlgorithmScenarioConfig((GU8)HBA_SCENES_DAILY_LIFE);
                        cur_scene = (uint8_t)HBA_SCENES_DAILY_LIFE;
                        motion_up_cnt = 0;
                        LOG_INF("ppg hr: scenario -> DAILY_LIFE (motion=%u)", (unsigned int)motion);
                    } else if ((cur_scene == (uint8_t)HBA_SCENES_DAILY_LIFE) &&
                               (motion_down_cnt >= APP_PPG_HR_SCENE_SWITCH_DOWN_N)) {
                        (void)GH3X2X_HbAlgorithmScenarioConfig((GU8)HBA_SCENES_STILL_REST);
                        cur_scene = (uint8_t)HBA_SCENES_STILL_REST;
                        motion_down_cnt = 0;
                        LOG_INF("ppg hr: scenario -> STILL_REST (motion=%u)", (unsigned int)motion);
                    }
                }
                last_scene_eval_ms = now_ms;
            }

            bool valid_acq = hr_is_physically_valid(&sample);
            bool valid_track = hr_is_physically_valid(&sample) && (sample.confidence >= APP_PPG_HR_CONF_TRACK_MIN);
            if (valid_acq && sample.confidence >= APP_PPG_HR_CONF_HARD) {
                valid_acq = true;
            } else if (valid_acq && sample.confidence >= APP_PPG_HR_CONF_SOFT) {
                if (!has_candidate) {
                    stable_cnt = 1;
                    last_candidate_hr = sample.hr_bpm;
                    has_candidate = true;
                } else {
                    if (i32_abs_diff(sample.hr_bpm, last_candidate_hr) <= APP_PPG_HR_STABLE_D) {
                        stable_cnt++;
                    } else {
                        stable_cnt = 1;
                    }
                    last_candidate_hr = sample.hr_bpm;
                }
                valid_acq = (stable_cnt >= APP_PPG_HR_STABLE_N);
            } else {
                valid_acq = false;
                stable_cnt = 0;
                has_candidate = false;
            }

            uint32_t elapsed_ms = (uint32_t)(now_ms - start_ms);
            bool aggressive_lock = (elapsed_ms <= APP_PPG_HR_AGGR_LOCK_WINDOW_MS);
            int32_t lock_conf_min = aggressive_lock ? APP_PPG_HR_AGGR_LOCK_CONF : APP_PPG_HR_CONF_LOCK;
            uint32_t lock_stable_n = aggressive_lock ? APP_PPG_HR_AGGR_LOCK_STABLE_N : 4U;
            bool lock_candidate = warmup_done &&
                                  valid_acq &&
                                  sample.confidence >= lock_conf_min &&
                                  sample.hr_bpm >= APP_PPG_HR_ACQ_MIN_BPM &&
                                  sample.hr_bpm <= APP_PPG_HR_ACQ_MAX_BPM;
            bool fast_lock_candidate = (elapsed_ms >= APP_PPG_HR_FAST_LOCK_MIN_MS) &&
                                       valid_acq &&
                                       sample.confidence >= APP_PPG_HR_FAST_LOCK_CONF &&
                                       sample.hr_bpm >= APP_PPG_HR_ACQ_MIN_BPM &&
                                       sample.hr_bpm <= APP_PPG_HR_ACQ_MAX_BPM;
            bool relax_lock_candidate = warmup_done &&
                                        (elapsed_ms >= APP_PPG_HR_RELAX_LOCK_MIN_MS) &&
                                        valid_track &&
                                        sample.confidence >= APP_PPG_HR_CONF_LOCK_RELAX &&
                                        sample.hr_bpm >= APP_PPG_HR_ACQ_MIN_BPM &&
                                        sample.hr_bpm <= APP_PPG_HR_ACQ_MAX_BPM;

            if (lock_candidate) {
                if (!has_candidate) {
                    stable_cnt = 1;
                    last_candidate_hr = sample.hr_bpm;
                    has_candidate = true;
                } else {
                    if (i32_abs_diff(sample.hr_bpm, last_candidate_hr) <= 6) {
                        stable_cnt++;
                    } else {
                        stable_cnt = 1;
                    }
                    last_candidate_hr = sample.hr_bpm;
                }

                if (mode == APP_PPG_MODE_ACQUIRE &&
                    (stable_cnt >= lock_stable_n ||
                     (fast_lock_candidate && stable_cnt >= APP_PPG_HR_FAST_LOCK_N) ||
                     (relax_lock_candidate && stable_cnt >= APP_PPG_HR_RELAX_LOCK_STABLE_N))) {
                    mode = APP_PPG_MODE_TRACK;
                    ppg_hr_set_state(APP_PPG_HR_ST_TRACKING);
                    last_valid_ms = now_ms;
                    LOG_INF("ppg hr: lock acquired (hr=%d conf=%d stable=%u elapsed=%ums), start output",
                            (int)sample.hr_bpm,
                            (int)sample.confidence,
                            (unsigned int)stable_cnt,
                            (unsigned int)elapsed_ms);
                }
            } else {
                if (mode == APP_PPG_MODE_ACQUIRE) {
                    stable_cnt = 0;
                    has_candidate = false;
                }
            }

            if ((mode == APP_PPG_MODE_TRACK) && valid_track) {
                last_valid_ms = now_ms;
            }

            bool should_output = (mode == APP_PPG_MODE_TRACK) && valid_track;
            if (!should_output) {
                if (warmup_done &&
                    sample.hr_bpm <= APP_PPG_HR_LOCK_BPM_MAX &&
                    sample.confidence >= APP_PPG_HR_LOCK_CONF_MIN) {
                    low_lock_cnt++;
                } else {
                    low_lock_cnt = 0;
                }

                if (warmup_done &&
                    sample.hr_bpm >= APP_PPG_HR_HIGH_LOCK_BPM_MIN &&
                    sample.confidence <= APP_PPG_HR_HIGH_LOCK_CONF_MAX) {
                    high_lock_cnt++;
                } else {
                    high_lock_cnt = 0;
                }

                if (mode == APP_PPG_MODE_ACQUIRE &&
                    warmup_done &&
                    sample.hr_bpm <= APP_PPG_HR_ACQ_STUCK_LOW_BPM &&
                    sample.confidence >= APP_PPG_HR_ACQ_STUCK_CONF_MIN) {
                    acq_low_cnt++;
                } else {
                    acq_low_cnt = 0;
                }

                bool recover_due_to_lock =
                    (mode == APP_PPG_MODE_TRACK) &&
                    ((low_lock_cnt >= APP_PPG_HR_LOCK_HOLD_N) ||
                     (high_lock_cnt >= APP_PPG_HR_HIGH_LOCK_HOLD_N));
                bool recover_due_to_no_valid =
                    (mode == APP_PPG_MODE_TRACK) &&
                    warmup_done &&
                    ((uint32_t)(now_ms - last_valid_ms) >= APP_PPG_HR_TRACK_NO_VALID_RECOVERY_MS);
                bool recover_due_to_acq_timeout =
                    (mode == APP_PPG_MODE_ACQUIRE) &&
                    ((uint32_t)(now_ms - start_ms) >= APP_PPG_HR_ACQ_RECOVERY_TIMEOUT_MS) &&
                    !(valid_track &&
                      sample.hr_bpm >= APP_PPG_HR_ACQ_MIN_BPM &&
                      sample.hr_bpm <= APP_PPG_HR_ACQ_MAX_BPM);
                bool recover_due_to_acq_first_retry =
                    (mode == APP_PPG_MODE_ACQUIRE) &&
                    (APP_PPG_HR_ACQ_FIRST_RETRY_MS > 0U) &&
                    (recover_cnt == 0U) &&
                    ((uint32_t)(now_ms - start_ms) >= APP_PPG_HR_ACQ_FIRST_RETRY_MS);
                bool recover_due_to_acq_stuck =
                    (mode == APP_PPG_MODE_ACQUIRE) &&
                    warmup_done &&
                    ((uint32_t)(now_ms - start_ms) >= APP_PPG_HR_ACQ_STUCK_MIN_MS) &&
                    (acq_low_cnt >= APP_PPG_HR_ACQ_STUCK_HOLD_N);
                bool recover_due_to_no_result =
                    ((uint32_t)(now_ms - last_result_ms) >= APP_PPG_HR_NO_VALID_RECOVERY_MS);

                if (warmup_done &&
                    (recover_due_to_lock || recover_due_to_no_valid ||
                     recover_due_to_acq_timeout || recover_due_to_acq_first_retry ||
                     recover_due_to_acq_stuck ||
                     recover_due_to_no_result) &&
                    ((uint32_t)(now_ms - last_recover_ms) >= APP_PPG_HR_RECOVERY_GAP_MS)) {
                    LOG_WRN("ppg hr: recovery trigger (hr=%d conf=%d low=%u high=%u no_valid_ms=%u), restart sampling",
                            (int)sample.hr_bpm,
                            (int)sample.confidence,
                            (unsigned int)low_lock_cnt,
                            (unsigned int)high_lock_cnt,
                            (unsigned int)(now_ms - last_valid_ms));
                    (void)hal_ppg_stop();
                    k_msleep(80);
                    if (hal_ppg_start() == HAL_OK) {
                        start_ms = k_uptime_get();
                        warmup_done_logged = false;
                        mode = APP_PPG_MODE_ACQUIRE;
                        ppg_hr_set_state(APP_PPG_HR_ST_LOCKING);
                        filtered_cnt = 0;
                        stable_cnt = 0;
                        has_candidate = false;
                        low_lock_cnt = 0;
                        high_lock_cnt = 0;
                        acq_low_cnt = 0;
                        last_valid_ms = start_ms;
                        last_recover_ms = now_ms;
                        last_scene_eval_ms = start_ms;
                        recover_cnt++;
                        LOG_WRN("ppg hr: recovery done #%u, warmup restart",
                                (unsigned int)recover_cnt);
                    } else {
                        LOG_ERR("ppg hr: recovery restart failed");
                    }
                }

                filtered_cnt++;
                if ((filtered_cnt % 20U) == 0U) {
                    LOG_DBG("ppg hr: filtering (mode=%d warmup=%d hr=%d conf=%d stable=%u cnt=%u)",
                            (int)mode,
                            warmup_done ? 1 : 0,
                            (int)sample.hr_bpm,
                            (int)sample.confidence,
                            (unsigned int)stable_cnt,
                            (unsigned int)filtered_cnt);
                }
                timeout_cnt = 0;
                last_timeout_log_ms = now_ms;
                continue;
            }

            valid_out_cnt++;
            int64_t now_log_ms = k_uptime_get();
            if ((uint32_t)(now_log_ms - last_ppg_log_ms) >= APP_PPG_LOG_PERIOD_MS) {
                LOG_INF("ppg hr: valid=%u bpm=%d conf=%d snr=%d frame=%u",
                        (unsigned int)valid_out_cnt,
                        (int)sample.hr_bpm,
                        (int)sample.confidence,
                        (int)sample.snr,
                        (unsigned int)sample.frame_id);
                valid_out_cnt = 0;
                last_ppg_log_ms = now_log_ms;
            }
            ppg_hr_set_latest(&sample);
            ppg_state_t ppg_state = {
                .sample = sample,
                .timestamp_ms = (uint32_t)k_uptime_get(),
                .valid = 1,
            };
            system_state_set_ppg(&ppg_state);

            app_event_t evt = {
                .id = APP_EVT_PPG_HR,
                .timestamp_ms = ppg_state.timestamp_ms,
                .data.ppg = sample,
            };
            (void)app_bus_publish(&evt);

            struct __packed {
                uint8_t ver;
                uint8_t type;
                int16_t hr_bpm;
                int16_t conf;
                int16_t snr;
                uint32_t frame_id;
                uint32_t ts_ms;
            } ppg_pkt = {
                .ver = 1,
                .type = 1,
                .hr_bpm = (int16_t)sample.hr_bpm,
                .conf = (int16_t)sample.confidence,
                .snr = (int16_t)sample.snr,
                .frame_id = sample.frame_id,
                .ts_ms = sample.timestamp_ms,
            };
            int uret = app_uplink_publish(APP_DATA_PART_PPG,
                                          APP_UPLINK_PRIO_HIGH,
                                          &ppg_pkt,
                                          sizeof(ppg_pkt),
                                          ppg_pkt.ts_ms);
            if (uret == HAL_OK) {
                ppg_tx_ok++;
            } else {
                ppg_tx_drop++;
            }
            low_lock_cnt = 0;
            high_lock_cnt = 0;
            last_valid_ms = now_ms;
            timeout_cnt = 0;
            last_timeout_log_ms = k_uptime_get();
            if ((uint32_t)(now_ms - last_uplink_log_ms) >= APP_PPG_LOG_PERIOD_MS) {
                LOG_INF("ppg uplink: tx_ok=%u tx_drop=%u ready=%d",
                        (unsigned int)ppg_tx_ok,
                        (unsigned int)ppg_tx_drop,
                        app_uplink_service_is_ready() ? 1 : 0);
                ppg_tx_ok = 0;
                ppg_tx_drop = 0;
                last_uplink_log_ms = now_ms;
            }
        } else if (ret == -ETIMEDOUT) {
            timeout_cnt++;
            int64_t now_ms = k_uptime_get();
            if ((now_ms - last_timeout_log_ms) >= 5000) {
                LOG_DBG("ppg hr: waiting result, queue timeout cnt=%u",
                        (unsigned int)timeout_cnt);
                last_timeout_log_ms = now_ms;
            }
        } else {
            LOG_WRN("ppg hr: read error=%d", ret);
        }
    }
}

int app_ppg_hr_start(void)
{
    static bool started;

    if (started) {
        return HAL_OK;
    }

    int ret = hal_ppg_init();
    if (ret != HAL_OK) {
        LOG_ERR("gh3026 init failed: %d", ret);
        return ret;
    }

    (void)GH3X2X_HbAlgorithmScenarioConfig((GU8)HBA_SCENES_STILL_REST);
    (void)GH3X2X_HbAlgorithmOutputTimeConfig(8, 2);

    k_mutex_init(&g_ppg_hr_state_lock);
    g_ppg_hr_state = APP_PPG_HR_ST_NOT_READY;
    g_ppg_hr_latest_valid = false;

    ret = hal_ppg_start();
    if (ret != HAL_OK) {
        LOG_ERR("gh3026 start failed: %d", ret);
        return ret;
    }

    ret = rt_thread_start(&g_ppg_hr_thread,
                          g_ppg_hr_stack,
                          K_THREAD_STACK_SIZEOF(g_ppg_hr_stack),
                          app_ppg_hr_thread_entry,
                          NULL, NULL, NULL,
                          APP_PPG_HR_PRIORITY, 0,
                          "ppg_hr");
    if (ret != 0) {
        LOG_ERR("ppg_hr thread start failed: %d", ret);
        return HAL_EIO;
    }

    started = true;
    LOG_INF("gh3026 app start");
    return HAL_OK;
}

app_ppg_hr_state_t app_ppg_hr_get_state(void)
{
    app_ppg_hr_state_t st;
    k_mutex_lock(&g_ppg_hr_state_lock, K_FOREVER);
    st = g_ppg_hr_state;
    k_mutex_unlock(&g_ppg_hr_state_lock);
    return st;
}

int app_ppg_hr_get_latest_sample(hal_ppg_sample_t *out)
{
    if (out == NULL) {
        return HAL_EINVAL;
    }

    int ret = HAL_OK;
    k_mutex_lock(&g_ppg_hr_state_lock, K_FOREVER);
    if (!g_ppg_hr_latest_valid) {
        ret = HAL_ENODEV;
    } else {
        *out = g_ppg_hr_latest;
    }
    k_mutex_unlock(&g_ppg_hr_state_lock);
    return ret;
}

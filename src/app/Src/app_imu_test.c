#include <math.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include "app_imu_test.h"
#include "app_uplink_service.h"
#include "error.h"
#include "hal_imu.h"
#include "invn_algo_agm.h"
#include "rt_thread.h"

LOG_MODULE_REGISTER(app_imu_test, LOG_LEVEL_WRN);

#define IMU_TEST_STACK_SIZE 2048
#define IMU_TEST_PRIORITY   8

#define IMU_TEMP_LSB_PER_C_X100 13248

#define IMU_LOG_EVERY_N  200
#define IMU_RATE_LOG_PERIOD_MS 5000
#define IMU_UPLINK_EVERY_N 4

#define IMU_ACC_LSB_PER_G 2048.0f
#define IMU_GYRO_LSB_PER_DPS 16.4f
#define IMU_GYRO_MOVE_SUM_DPS 4.5f
#define IMU_ACC_NORM_MOVING_G 0.12f

#define IMU_ACC_FSR_G        16
#define IMU_GYRO_FSR_DPS     2000
#define IMU_ODR_US           10000U
#define IMU_TEMP_OFFSET_Q16  (25 << 16)
#define IMU_TEMP_SENS_Q30    ((int32_t)(((int64_t)100 << 30) / IMU_TEMP_LSB_PER_C_X100))

#define IMU_ATT_FLAG_VALID      BIT(0)
#define IMU_ATT_FLAG_MOVING     BIT(1)
#define IMU_ATT_FLAG_BIAS_READY BIT(2)

#define IMU_SETTINGS_KEY "agm_bias"
#define IMU_SETTINGS_PATH "imu/agm_bias"
#define IMU_BIAS_BLOB_VERSION 1U
#define IMU_BIAS_SAVE_MIN_INTERVAL_MS 30000U

typedef struct {
    uint8_t version;
    int8_t acc_accuracy;
    int8_t gyr_accuracy;
    int8_t mag_accuracy;
    int32_t acc_bias_q16[3];
    int32_t gyr_bias_q16[3];
    int32_t mag_bias_q16[3];
} imu_agm_bias_blob_t;

typedef struct {
    bool inited;
    bool has_solution;
    bool settings_loaded;
    bool bias_restored;
    bool bias_save_valid;
    InvnAlgoAGMInput input;
    InvnAlgoAGMOutput output;
    int32_t acc_bias_q16[3];
    int32_t gyr_bias_q16[3];
    int32_t mag_bias_q16[3];
    imu_agm_bias_blob_t saved_bias_blob;
    uint32_t last_bias_save_ms;
} imu_agm_state_t;

static struct k_thread g_imu_thread;
RT_THREAD_STACK_DEFINE(g_imu_stack, IMU_TEST_STACK_SIZE);
static bool g_imu_started;
static bool g_imu_paused;
#if IS_ENABLED(CONFIG_IMU_TDK_AGM)
static imu_agm_state_t g_agm;

static int imu_settings_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
    imu_agm_bias_blob_t blob;
    int rc;

    if (settings_name_steq(key, IMU_SETTINGS_KEY, NULL) == 0) {
        return -ENOENT;
    }
    if (len != sizeof(blob)) {
        return -EINVAL;
    }

    rc = (int)read_cb(cb_arg, &blob, sizeof(blob));
    if (rc < 0) {
        return rc;
    }
    if (rc != sizeof(blob) || blob.version != IMU_BIAS_BLOB_VERSION) {
        return -EINVAL;
    }

    g_agm.saved_bias_blob = blob;
    g_agm.settings_loaded = true;
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(imu_agm, "imu", NULL, imu_settings_set, NULL, NULL);
#endif

#if IS_ENABLED(CONFIG_IMU_TDK_AGM)
static void imu_agm_restore_biases(void)
{
    if (!g_agm.settings_loaded || g_agm.saved_bias_blob.version != IMU_BIAS_BLOB_VERSION) {
        return;
    }

    memcpy(g_agm.acc_bias_q16, g_agm.saved_bias_blob.acc_bias_q16, sizeof(g_agm.acc_bias_q16));
    memcpy(g_agm.gyr_bias_q16, g_agm.saved_bias_blob.gyr_bias_q16, sizeof(g_agm.gyr_bias_q16));
    memcpy(g_agm.mag_bias_q16, g_agm.saved_bias_blob.mag_bias_q16, sizeof(g_agm.mag_bias_q16));
    g_agm.bias_restored = true;
    g_agm.bias_save_valid = true;
}

static void imu_agm_maybe_save_biases(uint32_t ts_ms)
{
    imu_agm_bias_blob_t blob;

    if (!g_agm.inited) {
        return;
    }
    if (g_agm.output.gyr_accuracy_flag < 2 && g_agm.output.acc_accuracy_flag < 2) {
        return;
    }
    if ((ts_ms - g_agm.last_bias_save_ms) < IMU_BIAS_SAVE_MIN_INTERVAL_MS) {
        return;
    }

    memset(&blob, 0, sizeof(blob));
    blob.version = IMU_BIAS_BLOB_VERSION;
    blob.acc_accuracy = g_agm.output.acc_accuracy_flag;
    blob.gyr_accuracy = g_agm.output.gyr_accuracy_flag;
    blob.mag_accuracy = g_agm.output.mag_accuracy_flag;
    memcpy(blob.acc_bias_q16, g_agm.output.acc_bias_q16, sizeof(blob.acc_bias_q16));
    memcpy(blob.gyr_bias_q16, g_agm.output.gyr_bias_q16, sizeof(blob.gyr_bias_q16));
    memcpy(blob.mag_bias_q16, g_agm.output.mag_bias_q16, sizeof(blob.mag_bias_q16));

    if (g_agm.bias_save_valid &&
        memcmp(&g_agm.saved_bias_blob, &blob, sizeof(blob)) == 0) {
        g_agm.last_bias_save_ms = ts_ms;
        return;
    }

    if (settings_save_one(IMU_SETTINGS_PATH, &blob, sizeof(blob)) == 0) {
        g_agm.saved_bias_blob = blob;
        g_agm.bias_save_valid = true;
        g_agm.last_bias_save_ms = ts_ms;
        LOG_INF("imu test: AGM bias saved acc=%d gyr=%d mag=%d",
                (int)blob.acc_accuracy, (int)blob.gyr_accuracy, (int)blob.mag_accuracy);
    }
}

static bool imu_sample_is_moving(const imu_sample_t *s)
{
    float ax_g = (float)s->accel_x / IMU_ACC_LSB_PER_G;
    float ay_g = (float)s->accel_y / IMU_ACC_LSB_PER_G;
    float az_g = (float)s->accel_z / IMU_ACC_LSB_PER_G;
    float gx_dps = (float)s->gyro_x / IMU_GYRO_LSB_PER_DPS;
    float gy_dps = (float)s->gyro_y / IMU_GYRO_LSB_PER_DPS;
    float gz_dps = (float)s->gyro_z / IMU_GYRO_LSB_PER_DPS;
    float gyro_sum = fabsf(gx_dps) + fabsf(gy_dps) + fabsf(gz_dps);
    float acc_norm = sqrtf(ax_g * ax_g + ay_g * ay_g + az_g * az_g);

    if (gyro_sum > IMU_GYRO_MOVE_SUM_DPS) {
        return true;
    }

    return fabsf(acc_norm - 1.0f) > IMU_ACC_NORM_MOVING_G;
}

static int imu_agm_init(void)
{
    InvnAlgoAGMConfig config;

    memset(&g_agm, 0, sizeof(g_agm));
    memset(&config, 0, sizeof(config));

    (void)settings_load_subtree("imu");
    imu_agm_restore_biases();

    config.acc_fsr = IMU_ACC_FSR_G;
    config.gyr_fsr = IMU_GYRO_FSR_DPS;
    config.acc_odr_us = IMU_ODR_US;
    config.gyr_odr_us = IMU_ODR_US;
    config.temp_sensitivity = IMU_TEMP_SENS_Q30;
    config.temp_offset = IMU_TEMP_OFFSET_Q16;
    config.acc_bias_q16 = g_agm.acc_bias_q16;
    config.gyr_bias_q16 = g_agm.gyr_bias_q16;
    config.mag_bias_q16 = g_agm.mag_bias_q16;
    config.acc_accuracy = g_agm.bias_restored ? g_agm.saved_bias_blob.acc_accuracy : 0;
    config.gyr_accuracy = g_agm.bias_restored ? g_agm.saved_bias_blob.gyr_accuracy : 0;
    config.mag_accuracy = g_agm.bias_restored ? g_agm.saved_bias_blob.mag_accuracy : 0;

    if (invn_algo_agm_init(&config) != 0U) {
        return HAL_EIO;
    }

    g_agm.inited = true;
    LOG_INF("imu test: TDK AGM init ok (%s)", invn_algo_agm_version());
    if (g_agm.bias_restored) {
        LOG_INF("imu test: AGM bias restored acc=%d gyr=%d mag=%d",
                (int)config.acc_accuracy, (int)config.gyr_accuracy, (int)config.mag_accuracy);
    }
    return HAL_OK;
}

static void imu_agm_step(const imu_sample_t *s, uint64_t ts_us)
{
    if (!g_agm.inited || (s == NULL)) {
        return;
    }

    memset(&g_agm.input, 0, sizeof(g_agm.input));
    g_agm.output.mask = 0;

    g_agm.input.mask = INVN_ALGO_AGM_INPUT_MASK_ACC | INVN_ALGO_AGM_INPUT_MASK_GYR;
    g_agm.input.sRimu_time_us = (int64_t)ts_us;
    g_agm.input.sRacc_data[0] = ((int32_t)s->accel_x) << 4;
    g_agm.input.sRacc_data[1] = ((int32_t)s->accel_y) << 4;
    g_agm.input.sRacc_data[2] = ((int32_t)s->accel_z) << 4;
    g_agm.input.sRgyr_data[0] = ((int32_t)s->gyro_x) << 4;
    g_agm.input.sRgyr_data[1] = ((int32_t)s->gyro_y) << 4;
    g_agm.input.sRgyr_data[2] = ((int32_t)s->gyro_z) << 4;
    g_agm.input.sRtemp_data = s->temp;

    invn_algo_agm_process(&g_agm.input, &g_agm.output);
    imu_agm_maybe_save_biases((uint32_t)(ts_us / 1000U));

    if ((g_agm.output.mask & INVN_ALGO_AGM_OUTPUT_MASK_QUAT_AG) != 0) {
        g_agm.has_solution = true;
    }
}

static void imu_publish_attitude(const imu_sample_t *s, uint32_t ts_ms, uint16_t seq)
{
    struct __packed {
        uint8_t ver;
        uint8_t type;
        uint16_t seq;
        int32_t qw_q30;
        int32_t qx_q30;
        int32_t qy_q30;
        int32_t qz_q30;
        int32_t gx_q16;
        int32_t gy_q16;
        int32_t gz_q16;
        int32_t lax_q16;
        int32_t lay_q16;
        int32_t laz_q16;
        uint8_t acc_accuracy;
        uint8_t gyr_accuracy;
        uint8_t mag_accuracy;
        uint8_t flags;
    } att_pkt;

    if (!g_agm.inited || !g_agm.has_solution || !app_uplink_service_is_ready() ||
        ((seq % CONFIG_IMU_ATTITUDE_UPLINK_EVERY_N) != 0U)) {
        return;
    }

    memset(&att_pkt, 0, sizeof(att_pkt));
    att_pkt.ver = 1;
    att_pkt.type = 1;
    att_pkt.seq = seq;
    att_pkt.qw_q30 = g_agm.output.grv_quat_q30[0];
    att_pkt.qx_q30 = g_agm.output.grv_quat_q30[1];
    att_pkt.qy_q30 = g_agm.output.grv_quat_q30[2];
    att_pkt.qz_q30 = g_agm.output.grv_quat_q30[3];
    att_pkt.gx_q16 = g_agm.output.gravity_q16[0];
    att_pkt.gy_q16 = g_agm.output.gravity_q16[1];
    att_pkt.gz_q16 = g_agm.output.gravity_q16[2];
    att_pkt.lax_q16 = g_agm.output.linear_acc_q16[0];
    att_pkt.lay_q16 = g_agm.output.linear_acc_q16[1];
    att_pkt.laz_q16 = g_agm.output.linear_acc_q16[2];
    att_pkt.acc_accuracy = (uint8_t)MAX(g_agm.output.acc_accuracy_flag, 0);
    att_pkt.gyr_accuracy = (uint8_t)MAX(g_agm.output.gyr_accuracy_flag, 0);
    att_pkt.mag_accuracy = (uint8_t)MAX(g_agm.output.mag_accuracy_flag, 0);
    att_pkt.flags = IMU_ATT_FLAG_VALID;

    if (imu_sample_is_moving(s)) {
        att_pkt.flags |= IMU_ATT_FLAG_MOVING;
    }
    if (g_agm.output.gyr_accuracy_flag >= 2) {
        att_pkt.flags |= IMU_ATT_FLAG_BIAS_READY;
    }

    (void)app_uplink_publish(APP_DATA_PART_ATTITUDE,
                             APP_UPLINK_PRIO_LOW,
                             &att_pkt,
                             sizeof(att_pkt),
                             ts_ms);
}
#endif

static void imu_test_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    printk("imu test: thread entry\n");
    LOG_INF("imu test: init");
    int ret = hal_imu_init();
    if (ret != HAL_OK) {
        LOG_ERR("imu test: hal_imu_init failed: %d", ret);
        return;
    }
#if IS_ENABLED(CONFIG_IMU_TDK_AGM)
    ret = imu_agm_init();
    if (ret != HAL_OK) {
        LOG_ERR("imu test: TDK AGM init failed: %d", ret);
        return;
    }
#endif
    LOG_INF("imu test: start raw sampling uplink (INT1, 100Hz)");

    uint32_t sample_cnt = 0;
    uint32_t err_cnt = 0;
    uint32_t timeout_cnt = 0;
    bool header_printed = false;
    int64_t rate_t0 = k_uptime_get();
    uint32_t rate_cnt = 0;
    int64_t last_raw_log_ms = 0;
    uint64_t last_sample_ts_us = 0U;

    while (1) {
        if (g_imu_paused) {
            k_msleep(20);
            continue;
        }

        imu_sample_t s = {0};
        uint64_t sample_ts_us = 0U;
        ret = hal_imu_read_timed(&s, sizeof(s), &sample_ts_us, 1000);
        if (ret == HAL_ENOTSUP) {
            ret = hal_imu_read(&s, sizeof(s), 1000);
        }
        if (ret != HAL_OK) {
            if (g_imu_paused) {
                k_msleep(20);
                continue;
            }
            if (ret == HAL_EBUSY) {
                timeout_cnt++;
                if ((timeout_cnt % 30U) == 0U) {
                    LOG_WRN("imu test: waiting for DRDY (timeouts=%u)", timeout_cnt);
                }
                continue;
            }
            err_cnt++;
            LOG_ERR("imu test: read failed: %d (errs=%u)", ret, err_cnt);
            continue;
        }

        sample_cnt++;
        rate_cnt++;

        if (sample_ts_us == 0U &&
            (hal_imu_get_latest_us(&s, &sample_ts_us) != HAL_OK || sample_ts_us == 0U)) {
            sample_ts_us = k_cyc_to_us_floor64(k_cycle_get_64());
        }
        last_sample_ts_us = sample_ts_us;

        int64_t now = (int64_t)(sample_ts_us / 1000U);
        if ((now - rate_t0) >= IMU_RATE_LOG_PERIOD_MS) {
            uint32_t elapsed_ms = (uint32_t)(now - rate_t0);
            uint32_t sps = (elapsed_ms > 0U) ? (rate_cnt * 1000U / elapsed_ms) : 0U;
#if !IS_ENABLED(CONFIG_PPG_TUNE_MODE)
            LOG_INF("imu_rate,sps=%u (samples=%u, ms=%u)", sps, rate_cnt, elapsed_ms);
#endif
            rate_cnt = 0;
            rate_t0 = now;
        }

        int32_t t_centi = (int32_t)s.temp * 10000 / IMU_TEMP_LSB_PER_C_X100 + 2500;
        int32_t t_int = t_centi / 100;
        int32_t t_abs = (t_centi >= 0) ? t_centi : -t_centi;
        int32_t t_frac = t_abs % 100;

#if IS_ENABLED(CONFIG_IMU_TDK_AGM)
        imu_agm_step(&s, sample_ts_us);
        if (IS_ENABLED(CONFIG_IMU_ATTITUDE_UPLINK)) {
            imu_publish_attitude(&s, (uint32_t)now, (uint16_t)(sample_cnt & 0xFFFFU));
        }
#endif

        if (!header_printed) {
#if !IS_ENABLED(CONFIG_PPG_TUNE_MODE)
            LOG_INF("imu_raw,seq,ax_lsb,ay_lsb,az_lsb,gx_lsb,gy_lsb,gz_lsb,temp_lsb,temp_C");
#endif
            header_printed = true;
        }

        if ((sample_cnt % IMU_LOG_EVERY_N) == 0U) {
#if !IS_ENABLED(CONFIG_PPG_TUNE_MODE)
            LOG_INF("imu_raw,%u,%d,%d,%d,%d,%d,%d,%d,%d.%02d",
                    sample_cnt,
                    s.accel_x, s.accel_y, s.accel_z,
                    s.gyro_x, s.gyro_y, s.gyro_z,
                    s.temp, t_int, t_frac);
#endif
        }

        if ((sample_cnt % IMU_UPLINK_EVERY_N) == 0U &&
            app_uplink_service_is_ready()) {
            int64_t up_now = k_uptime_get();
            struct __packed {
                uint8_t ver;
                uint8_t type;
                uint16_t seq;
                int16_t ax;
                int16_t ay;
                int16_t az;
                int16_t gx;
                int16_t gy;
                int16_t gz;
                int16_t temp;
                uint32_t ts_ms;
            } imu_raw_pkt = {
                .ver = 1,
                .type = 3,
                .seq = (uint16_t)(sample_cnt & 0xFFFFU),
                .ax = s.accel_x,
                .ay = s.accel_y,
                .az = s.accel_z,
                .gx = s.gyro_x,
                .gy = s.gyro_y,
                .gz = s.gyro_z,
                .temp = s.temp,
                .ts_ms = (uint32_t)(last_sample_ts_us / 1000U),
            };
            (void)app_uplink_publish(APP_DATA_PART_IMU,
                                     APP_UPLINK_PRIO_LOW,
                                     &imu_raw_pkt,
                                     sizeof(imu_raw_pkt),
                                     imu_raw_pkt.ts_ms);
            if ((up_now - last_raw_log_ms) >= 1000) {
                last_raw_log_ms = up_now;
                LOG_INF("imu_raw uplink: seq=%u ax=%d ay=%d az=%d gx=%d gy=%d gz=%d temp=%d",
                        (unsigned)imu_raw_pkt.seq,
                        imu_raw_pkt.ax, imu_raw_pkt.ay, imu_raw_pkt.az,
                        imu_raw_pkt.gx, imu_raw_pkt.gy, imu_raw_pkt.gz,
                        imu_raw_pkt.temp);
            }
        }
        k_yield();
    }
}

void app_imu_test_start(void)
{
    if (g_imu_started) {
        return;
    }

    int ret = rt_thread_start(&g_imu_thread,
                              g_imu_stack,
                              K_THREAD_STACK_SIZEOF(g_imu_stack),
                              imu_test_entry,
                              NULL, NULL, NULL,
                              IMU_TEST_PRIORITY, 0,
                              "imu_test");
    if (ret != 0) {
        LOG_ERR("imu test: thread start failed: %d", ret);
        return;
    }

    g_imu_started = true;
}

void app_imu_test_pause(void)
{
    if (!g_imu_started || g_imu_paused) {
        return;
    }
    g_imu_paused = true;
}

void app_imu_test_resume(void)
{
    if (!g_imu_started || !g_imu_paused) {
        return;
    }
    g_imu_paused = false;
}

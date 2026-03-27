#include <math.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "app_imu_test.h"
#include "hal_imu.h"
#include "error.h"
#include "rt_thread.h"
#include "app_uplink_service.h"

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
#define IMU_BIAS_SAMPLES_REQUIRED 80U
#define IMU_FUSION_BETA 0.12f
#define IMU_PI 3.14159265358979323846f

#define IMU_ATT_FLAG_VALID      BIT(0)
#define IMU_ATT_FLAG_MOVING     BIT(1)
#define IMU_ATT_FLAG_BIAS_READY BIT(2)

typedef struct {
    float w;
    float x;
    float y;
    float z;
} imu_quat_t;

typedef struct {
    bool inited;
    bool bias_ready;
    imu_quat_t q;
    uint32_t bias_count;
    float gyro_bias_dps[3];
    float gyro_bias_accum[3];
    uint32_t last_ts_ms;
} imu_fusion_state_t;

static struct k_thread g_imu_thread;
RT_THREAD_STACK_DEFINE(g_imu_stack, IMU_TEST_STACK_SIZE);
static bool g_imu_started;
static bool g_imu_paused;
#if IS_ENABLED(CONFIG_IMU_TDK_AGM)
static imu_fusion_state_t g_fusion;
#endif

#if IS_ENABLED(CONFIG_IMU_TDK_AGM)
static float imu_absf(float v)
{
    return (v >= 0.0f) ? v : -v;
}

static void imu_quat_normalize(imu_quat_t *q)
{
    float norm = sqrtf(q->w * q->w + q->x * q->x + q->y * q->y + q->z * q->z);
    if (norm <= 0.0f) {
        q->w = 1.0f;
        q->x = 0.0f;
        q->y = 0.0f;
        q->z = 0.0f;
        return;
    }

    q->w /= norm;
    q->x /= norm;
    q->y /= norm;
    q->z /= norm;
}

static void imu_fusion_init(void)
{
    memset(&g_fusion, 0, sizeof(g_fusion));
    g_fusion.q.w = 1.0f;
    g_fusion.inited = true;
}

static bool imu_fusion_is_moving(float ax_g, float ay_g, float az_g,
                                 float gx_dps, float gy_dps, float gz_dps)
{
    float gyro_sum = imu_absf(gx_dps) + imu_absf(gy_dps) + imu_absf(gz_dps);
    if (gyro_sum > IMU_GYRO_MOVE_SUM_DPS) {
        return true;
    }

    float acc_norm = sqrtf(ax_g * ax_g + ay_g * ay_g + az_g * az_g);
    return imu_absf(acc_norm - 1.0f) > IMU_ACC_NORM_MOVING_G;
}

static void imu_fusion_update_biases(bool moving, const float gyro_dps[3])
{
    if (g_fusion.bias_ready || moving) {
        return;
    }

    g_fusion.gyro_bias_accum[0] += gyro_dps[0];
    g_fusion.gyro_bias_accum[1] += gyro_dps[1];
    g_fusion.gyro_bias_accum[2] += gyro_dps[2];
    g_fusion.bias_count++;

    if (g_fusion.bias_count >= IMU_BIAS_SAMPLES_REQUIRED) {
        g_fusion.gyro_bias_dps[0] = g_fusion.gyro_bias_accum[0] / (float)g_fusion.bias_count;
        g_fusion.gyro_bias_dps[1] = g_fusion.gyro_bias_accum[1] / (float)g_fusion.bias_count;
        g_fusion.gyro_bias_dps[2] = g_fusion.gyro_bias_accum[2] / (float)g_fusion.bias_count;
        g_fusion.bias_ready = true;
    }
}

static void imu_fusion_step(const imu_sample_t *s, uint32_t ts_ms)
{
    if (!g_fusion.inited || s == NULL) {
        return;
    }

    float dt = 0.0f;
    if (g_fusion.last_ts_ms != 0U) {
        uint32_t delta_ms = ts_ms - g_fusion.last_ts_ms;
        if (delta_ms > 100U) {
            delta_ms = 100U;
        }
        dt = (float)delta_ms / 1000.0f;
    }
    g_fusion.last_ts_ms = ts_ms;

    float ax = (float)s->accel_x / IMU_ACC_LSB_PER_G;
    float ay = (float)s->accel_y / IMU_ACC_LSB_PER_G;
    float az = (float)s->accel_z / IMU_ACC_LSB_PER_G;
    float gyro_dps[3] = {
        (float)s->gyro_x / IMU_GYRO_LSB_PER_DPS,
        (float)s->gyro_y / IMU_GYRO_LSB_PER_DPS,
        (float)s->gyro_z / IMU_GYRO_LSB_PER_DPS,
    };

    bool moving = imu_fusion_is_moving(ax, ay, az, gyro_dps[0], gyro_dps[1], gyro_dps[2]);
    imu_fusion_update_biases(moving, gyro_dps);

    if (dt <= 0.0f) {
        return;
    }

    float q0 = g_fusion.q.w;
    float q1 = g_fusion.q.x;
    float q2 = g_fusion.q.y;
    float q3 = g_fusion.q.z;

    float gx = (gyro_dps[0] - g_fusion.gyro_bias_dps[0]) * (IMU_PI / 180.0f);
    float gy = (gyro_dps[1] - g_fusion.gyro_bias_dps[1]) * (IMU_PI / 180.0f);
    float gz = (gyro_dps[2] - g_fusion.gyro_bias_dps[2]) * (IMU_PI / 180.0f);

    float acc_norm = sqrtf(ax * ax + ay * ay + az * az);
    if (acc_norm > 0.0f) {
        ax /= acc_norm;
        ay /= acc_norm;
        az /= acc_norm;

        float two_q0 = 2.0f * q0;
        float two_q1 = 2.0f * q1;
        float two_q2 = 2.0f * q2;
        float two_q3 = 2.0f * q3;
        float four_q0 = 4.0f * q0;
        float four_q1 = 4.0f * q1;
        float four_q2 = 4.0f * q2;
        float eight_q1 = 8.0f * q1;
        float eight_q2 = 8.0f * q2;
        float q0q0 = q0 * q0;
        float q1q1 = q1 * q1;
        float q2q2 = q2 * q2;
        float q3q3 = q3 * q3;

        float s0 = four_q0 * q2q2 + two_q2 * ax + four_q0 * q1q1 - two_q1 * ay;
        float s1 = four_q1 * q3q3 - two_q3 * ax + 4.0f * q0q0 * q1 - two_q0 * ay - four_q1 +
                   eight_q1 * q1q1 + eight_q1 * q2q2 + four_q1 * az;
        float s2 = 4.0f * q0q0 * q2 + two_q0 * ax + four_q2 * q3q3 - two_q3 * ay - four_q2 +
                   eight_q2 * q1q1 + eight_q2 * q2q2 + four_q2 * az;
        float s3 = 4.0f * q1q1 * q3 - two_q1 * ax + 4.0f * q2q2 * q3 - two_q2 * ay;

        float s_norm = sqrtf(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
        if (s_norm > 0.0f) {
            s0 /= s_norm;
            s1 /= s_norm;
            s2 /= s_norm;
            s3 /= s_norm;

            float q_dot0 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz) - IMU_FUSION_BETA * s0;
            float q_dot1 = 0.5f * (q0 * gx + q2 * gz - q3 * gy) - IMU_FUSION_BETA * s1;
            float q_dot2 = 0.5f * (q0 * gy - q1 * gz + q3 * gx) - IMU_FUSION_BETA * s2;
            float q_dot3 = 0.5f * (q0 * gz + q1 * gy - q2 * gx) - IMU_FUSION_BETA * s3;

            g_fusion.q.w = q0 + q_dot0 * dt;
            g_fusion.q.x = q1 + q_dot1 * dt;
            g_fusion.q.y = q2 + q_dot2 * dt;
            g_fusion.q.z = q3 + q_dot3 * dt;
            imu_quat_normalize(&g_fusion.q);
            return;
        }
    }

    g_fusion.q.w = q0 + 0.5f * (-q1 * gx - q2 * gy - q3 * gz) * dt;
    g_fusion.q.x = q1 + 0.5f * (q0 * gx + q2 * gz - q3 * gy) * dt;
    g_fusion.q.y = q2 + 0.5f * (q0 * gy - q1 * gz + q3 * gx) * dt;
    g_fusion.q.z = q3 + 0.5f * (q0 * gz + q1 * gy - q2 * gx) * dt;
    imu_quat_normalize(&g_fusion.q);
}

static int32_t imu_float_to_q30(float v)
{
    float scaled = v * 1073741824.0f;
    if (scaled > 2147483647.0f) {
        scaled = 2147483647.0f;
    } else if (scaled < -2147483648.0f) {
        scaled = -2147483648.0f;
    }
    return (int32_t)scaled;
}

static int32_t imu_float_to_q16(float v)
{
    float scaled = v * 65536.0f;
    if (scaled > 2147483647.0f) {
        scaled = 2147483647.0f;
    } else if (scaled < -2147483648.0f) {
        scaled = -2147483648.0f;
    }
    return (int32_t)scaled;
}

static void imu_compute_gravity_and_linear(const imu_sample_t *s,
                                           float gravity_g[3],
                                           float linear_g[3])
{
    const float q0 = g_fusion.q.w;
    const float q1 = g_fusion.q.x;
    const float q2 = g_fusion.q.y;
    const float q3 = g_fusion.q.z;

    gravity_g[0] = 2.0f * (q1 * q3 - q0 * q2);
    gravity_g[1] = 2.0f * (q0 * q1 + q2 * q3);
    gravity_g[2] = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;

    float acc_g[3] = {
        (float)s->accel_x / IMU_ACC_LSB_PER_G,
        (float)s->accel_y / IMU_ACC_LSB_PER_G,
        (float)s->accel_z / IMU_ACC_LSB_PER_G,
    };
    linear_g[0] = acc_g[0] - gravity_g[0];
    linear_g[1] = acc_g[1] - gravity_g[1];
    linear_g[2] = acc_g[2] - gravity_g[2];
}

static void imu_publish_attitude(const imu_sample_t *s, uint32_t ts_ms, uint16_t seq)
{
    if (!g_fusion.inited || !app_uplink_service_is_ready() ||
        ((seq % CONFIG_IMU_ATTITUDE_UPLINK_EVERY_N) != 0U)) {
        return;
    }

    float gyro_dps[3] = {
        (float)s->gyro_x / IMU_GYRO_LSB_PER_DPS,
        (float)s->gyro_y / IMU_GYRO_LSB_PER_DPS,
        (float)s->gyro_z / IMU_GYRO_LSB_PER_DPS,
    };
    float acc_g[3] = {
        (float)s->accel_x / IMU_ACC_LSB_PER_G,
        (float)s->accel_y / IMU_ACC_LSB_PER_G,
        (float)s->accel_z / IMU_ACC_LSB_PER_G,
    };
    bool moving = imu_fusion_is_moving(acc_g[0], acc_g[1], acc_g[2],
                                       gyro_dps[0], gyro_dps[1], gyro_dps[2]);
    float gravity_g[3];
    float linear_g[3];
    imu_compute_gravity_and_linear(s, gravity_g, linear_g);

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
    } att_pkt = {
        .ver = 1,
        .type = 1,
        .seq = seq,
        .qw_q30 = imu_float_to_q30(g_fusion.q.w),
        .qx_q30 = imu_float_to_q30(g_fusion.q.x),
        .qy_q30 = imu_float_to_q30(g_fusion.q.y),
        .qz_q30 = imu_float_to_q30(g_fusion.q.z),
        .gx_q16 = imu_float_to_q16(gravity_g[0]),
        .gy_q16 = imu_float_to_q16(gravity_g[1]),
        .gz_q16 = imu_float_to_q16(gravity_g[2]),
        .lax_q16 = imu_float_to_q16(linear_g[0]),
        .lay_q16 = imu_float_to_q16(linear_g[1]),
        .laz_q16 = imu_float_to_q16(linear_g[2]),
        .acc_accuracy = g_fusion.bias_ready ? 3U : 0U,
        .gyr_accuracy = g_fusion.bias_ready ? 2U : 0U,
        .mag_accuracy = 0U,
        .flags = IMU_ATT_FLAG_VALID,
    };

    if (moving) {
        att_pkt.flags |= IMU_ATT_FLAG_MOVING;
    }
    if (g_fusion.bias_ready) {
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
    imu_fusion_init();
    LOG_WRN("imu test: board-side 6-axis fusion enabled; TDK hard-float library is not linked in this softfp build");
#endif
    LOG_INF("imu test: start raw sampling uplink (INT1, 100Hz)");

    uint32_t sample_cnt = 0;
    uint32_t err_cnt = 0;
    uint32_t timeout_cnt = 0;
    bool header_printed = false;
    int64_t rate_t0 = k_uptime_get();
    uint32_t rate_cnt = 0;
    int64_t last_raw_log_ms = 0;

    while (1) {
        if (g_imu_paused) {
            k_msleep(20);
            continue;
        }

        imu_sample_t s = {0};
        ret = hal_imu_read(&s, sizeof(s), 1000);
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

        int64_t now = k_uptime_get();
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
        imu_fusion_step(&s, (uint32_t)now);
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
                .ts_ms = (uint32_t)up_now,
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

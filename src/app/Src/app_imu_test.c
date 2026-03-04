#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_imu_test.h"
#include "hal_imu.h"
#include "error.h"
#include "rt_thread.h"
#include "imu_algo.h"
#include "app_uplink_service.h"

LOG_MODULE_REGISTER(app_imu_test, LOG_LEVEL_INF);

#define IMU_TEST_STACK_SIZE 2048
#define IMU_TEST_PRIORITY   5

#define IMU_ACCEL_LSB_PER_G 2048
#define IMU_GYRO_LSB_PER_DPS_X10 164
#define IMU_TEMP_LSB_PER_C_X100 13248

#define IMU_CALIB_SAMPLES 200
#define IMU_LOG_EVERY_N  200
#define IMU_ACTION_LOG_EVERY_N 100
#define IMU_RATE_LOG_PERIOD_MS 5000
#define IMU_UPLINK_EVERY_N 20

static struct k_thread g_imu_thread;
RT_THREAD_STACK_DEFINE(g_imu_stack, IMU_TEST_STACK_SIZE);

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

    LOG_INF("imu test: start sampling (INT1, 100Hz)");
    ret = imu_algo_init();
    if (ret != HAL_OK) {
        LOG_ERR("imu test: imu_algo_init failed: %d", ret);
        return;
    }

    uint32_t sample_cnt = 0;
    uint32_t err_cnt = 0;
    uint32_t timeout_cnt = 0;
    bool calib_done = false;
    int64_t ax_bias = 0;
    int64_t ay_bias = 0;
    int64_t az_bias = 0;
    int64_t gx_bias = 0;
    int64_t gy_bias = 0;
    int64_t gz_bias = 0;
    int64_t ax_sum = 0;
    int64_t ay_sum = 0;
    int64_t az_sum = 0;
    int64_t gx_sum = 0;
    int64_t gy_sum = 0;
    int64_t gz_sum = 0;

    int32_t ax_f = 0;
    int32_t ay_f = 0;
    int32_t az_f = 0;
    int32_t gx_f = 0;
    int32_t gy_f = 0;
    int32_t gz_f = 0;
    bool header_printed = false;
    imu_action_t last_action = IMU_ACTION_UNKNOWN;
    int64_t rate_t0 = k_uptime_get();
    uint32_t rate_cnt = 0;
    uint32_t action_cnt = 0;
    int64_t last_action_log_ms = 0;

    while (1) {
        imu_sample_t s = {0};
        ret = hal_imu_read(&s, sizeof(s), 1000);
        if (ret != HAL_OK) {
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
            LOG_INF("imu_rate,sps=%u (samples=%u, ms=%u)", sps, rate_cnt, elapsed_ms);
            rate_cnt = 0;
            rate_t0 = now;
        }

        if (!calib_done) {
            ax_sum += s.accel_x;
            ay_sum += s.accel_y;
            az_sum += s.accel_z;
            gx_sum += s.gyro_x;
            gy_sum += s.gyro_y;
            gz_sum += s.gyro_z;
            if (sample_cnt >= IMU_CALIB_SAMPLES) {
                ax_bias = ax_sum / IMU_CALIB_SAMPLES;
                ay_bias = ay_sum / IMU_CALIB_SAMPLES;
                az_bias = (az_sum / IMU_CALIB_SAMPLES) - IMU_ACCEL_LSB_PER_G;
                gx_bias = gx_sum / IMU_CALIB_SAMPLES;
                gy_bias = gy_sum / IMU_CALIB_SAMPLES;
                gz_bias = gz_sum / IMU_CALIB_SAMPLES;
                calib_done = true;
                LOG_INF("imu calib done: abias(%lld,%lld,%lld) gbias(%lld,%lld,%lld)",
                        ax_bias, ay_bias, az_bias, gx_bias, gy_bias, gz_bias);
            }
            continue;
        }

        int32_t ax = (int32_t)(s.accel_x - ax_bias);
        int32_t ay = (int32_t)(s.accel_y - ay_bias);
        int32_t az = (int32_t)(s.accel_z - az_bias);
        int32_t gx = (int32_t)(s.gyro_x - gx_bias);
        int32_t gy = (int32_t)(s.gyro_y - gy_bias);
        int32_t gz = (int32_t)(s.gyro_z - gz_bias);

        ax_f = (ax_f * 3 + ax) / 4;
        ay_f = (ay_f * 3 + ay) / 4;
        az_f = (az_f * 3 + az) / 4;
        gx_f = (gx_f * 3 + gx) / 4;
        gy_f = (gy_f * 3 + gy) / 4;
        gz_f = (gz_f * 3 + gz) / 4;

        int32_t t_centi = (int32_t)s.temp * 10000 / IMU_TEMP_LSB_PER_C_X100 + 2500;
        int32_t t_int = t_centi / 100;
        int32_t t_abs = (t_centi >= 0) ? t_centi : -t_centi;
        int32_t t_frac = t_abs % 100;

        if (!header_printed) {
            LOG_INF("imu_raw,seq,ax_lsb,ay_lsb,az_lsb,gx_lsb,gy_lsb,gz_lsb,temp_lsb,temp_C");
            header_printed = true;
        }

        if ((sample_cnt % IMU_LOG_EVERY_N) == 0U) {
            LOG_INF("imu_raw,%u,%d,%d,%d,%d,%d,%d,%d,%d.%02d",
                    sample_cnt,
                    s.accel_x, s.accel_y, s.accel_z,
                    s.gyro_x, s.gyro_y, s.gyro_z,
                    s.temp, t_int, t_frac);
        }

        imu_algo_input_t algo_in = {
            .accel_x_lsb = ax_f,
            .accel_y_lsb = ay_f,
            .accel_z_lsb = az_f,
            .gyro_x_lsb = gx_f,
            .gyro_y_lsb = gy_f,
            .gyro_z_lsb = gz_f,
            .temp_centi_deg = t_centi,
            .timestamp_ms = (uint32_t)k_uptime_get(),
            .dt_ms = 10U,
        };
        imu_algo_output_t algo_out = {0};
        ret = imu_algo_process(&algo_in, &algo_out);
        if (ret == HAL_OK && algo_out.valid) {
            action_cnt++;
            int64_t act_now = k_uptime_get();
            bool action_changed = (algo_out.action != last_action);
            bool periodic_log = ((action_cnt % IMU_ACTION_LOG_EVERY_N) == 0U);
            bool changed_log_allowed = action_changed &&
                                       ((act_now - last_action_log_ms) >= 1000);
            if (periodic_log || changed_log_allowed) {
                LOG_INF("imu_action,seq=%u,action=%s,conf=%u",
                        sample_cnt,
                        imu_action_name(algo_out.action),
                        algo_out.confidence);
                last_action = algo_out.action;
                last_action_log_ms = act_now;
            }

            if (action_changed || ((sample_cnt % IMU_UPLINK_EVERY_N) == 0U)) {
                struct __packed {
                    uint8_t ver;
                    uint8_t type;
                    uint16_t seq;
                    uint8_t action;
                    uint8_t conf;
                    uint32_t ts_ms;
                } imu_pkt = {
                    .ver = 1,
                    .type = 2,
                    .seq = (uint16_t)(sample_cnt & 0xFFFFU),
                    .action = (uint8_t)algo_out.action,
                    .conf = algo_out.confidence,
                    .ts_ms = (uint32_t)act_now,
                };
                (void)app_uplink_publish(APP_DATA_PART_IMU,
                                         APP_UPLINK_PRIO_LOW,
                                         &imu_pkt,
                                         sizeof(imu_pkt),
                                         imu_pkt.ts_ms);
            }
        }
    }
}

void app_imu_test_start(void)
{
    static bool started;
    if (started) {
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

    started = true;
}

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

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
#define IMU_UPLINK_EVERY_N 20

static struct k_thread g_imu_thread;
RT_THREAD_STACK_DEFINE(g_imu_stack, IMU_TEST_STACK_SIZE);
static bool g_imu_started;
static bool g_imu_paused;

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
                .type = 3, /* raw 6-axis + temp */
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

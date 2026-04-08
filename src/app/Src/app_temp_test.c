#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_temp_test.h"
#include "app_uplink_service.h"
#include "error.h"
#include "hal_temp.h"
#include "rt_thread.h"

#if IS_ENABLED(CONFIG_TEMP_TEST)
LOG_MODULE_REGISTER(app_temp_test, LOG_LEVEL_INF);
#else
LOG_MODULE_REGISTER(app_temp_test, LOG_LEVEL_WRN);
#endif

#define TEMP_TEST_STACK_SIZE 1536
#define TEMP_TEST_PRIORITY   8
#define TEMP_TEST_PERIOD_MS  1000
#define TEMP_READ_TIMEOUT_MS 1200

static struct k_thread g_temp_thread;
RT_THREAD_STACK_DEFINE(g_temp_stack, TEMP_TEST_STACK_SIZE);
static bool g_temp_started;
static uint16_t g_temp_seq;

static void temp_test_log_sample(const hal_temp_sample_t *sample)
{
    int32_t micro_c = sample->micro_c;
    int32_t abs_micro = (micro_c >= 0) ? micro_c : -micro_c;
    int32_t whole = micro_c / 1000000;
    int32_t frac4 = (abs_micro % 1000000) / 100;

    LOG_INF("tmp119 temp: raw=0x%04x temp=%d.%04d C ts=%u ms",
            (uint16_t)sample->raw,
            whole,
            frac4,
            (unsigned int)sample->timestamp_ms);
}

static void temp_test_publish_sample(const hal_temp_sample_t *sample)
{
    struct __packed {
        uint8_t ver;
        uint8_t type;
        uint16_t seq;
        int16_t raw;
        int32_t micro_c;
        uint32_t ts_ms;
    } pkt = {
        .ver = 1,
        .type = 1,
        .seq = g_temp_seq++,
        .raw = sample->raw,
        .micro_c = sample->micro_c,
        .ts_ms = sample->timestamp_ms,
    };

    if (!app_uplink_service_is_ready()) {
        return;
    }

    (void)app_uplink_publish(APP_DATA_PART_TEMP,
                             APP_UPLINK_PRIO_LOW,
                             &pkt,
                             sizeof(pkt),
                             pkt.ts_ms);
}

static void temp_test_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    int ret = hal_temp_init();
    if (ret != HAL_OK) {
        LOG_ERR("tmp119 test: hal_temp_init failed: %d", ret);
        return;
    }

    uint16_t device_id = 0U;
    ret = hal_temp_get_device_id(&device_id);
    if (ret == HAL_OK) {
        LOG_INF("tmp119 test: device id=0x%04x", device_id);
    }

    while (1) {
        hal_temp_sample_t sample = {0};
        ret = hal_temp_read(&sample, TEMP_READ_TIMEOUT_MS);
        if (ret == HAL_OK) {
            temp_test_log_sample(&sample);
            temp_test_publish_sample(&sample);
        } else if (ret == HAL_EBUSY) {
            LOG_WRN("tmp119 test: waiting for data-ready timeout");
        } else {
            LOG_ERR("tmp119 test: read failed: %d", ret);
        }
        k_msleep(TEMP_TEST_PERIOD_MS);
    }
}

void app_temp_test_start(void)
{
    if (g_temp_started) {
        return;
    }

    int ret = rt_thread_start(&g_temp_thread,
                              g_temp_stack,
                              K_THREAD_STACK_SIZEOF(g_temp_stack),
                              temp_test_entry,
                              NULL, NULL, NULL,
                              TEMP_TEST_PRIORITY, 0,
                              "temp_test");
    if (ret != 0) {
        LOG_ERR("tmp119 test: thread start failed: %d", ret);
        return;
    }

    g_temp_started = true;
}

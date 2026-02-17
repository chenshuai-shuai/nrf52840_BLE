#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "hal_audio.h"
#include "error.h"
#include "driver_stats.h"
#include "audio_jitter.h"

#define AUDIO_STATS_EXPECTED_PERIOD_US 1000U

static const hal_audio_ops_t *g_audio_ops;
static DRIVER_STATS_DEFINE(g_audio_stats);
static audio_jitter_t g_audio_jitter;
static atomic_t g_audio_monitor_inited;

static void audio_monitor_init_once(void)
{
    if (atomic_cas(&g_audio_monitor_inited, 0, 1)) {
        driver_stats_init(&g_audio_stats);
        audio_jitter_init(&g_audio_jitter, AUDIO_STATS_EXPECTED_PERIOD_US);
    }
}

int hal_audio_register(const hal_audio_ops_t *ops)
{
    if (ops == NULL || ops->init == NULL) {
        return HAL_EINVAL;
    }
    if (g_audio_ops != NULL) {
        return HAL_EBUSY;
    }

    g_audio_ops = ops;
    return HAL_OK;
}

int hal_audio_init(void)
{
    if (g_audio_ops == NULL || g_audio_ops->init == NULL) {
        return HAL_ENODEV;
    }

    audio_monitor_init_once();

    int ret = g_audio_ops->init();
    if (ret < 0) {
        driver_stats_record_err(&g_audio_stats, ret);
    } else {
        driver_stats_record_ok(&g_audio_stats);
    }
    return ret;
}

int hal_audio_start(hal_audio_dir_t dir, const hal_audio_cfg_t *cfg)
{
    if (g_audio_ops == NULL || g_audio_ops->start == NULL) {
        return HAL_ENODEV;
    }

    int ret = g_audio_ops->start(dir, cfg);
    if (ret < 0) {
        driver_stats_record_err(&g_audio_stats, ret);
    } else {
        driver_stats_record_ok(&g_audio_stats);
    }
    return ret;
}

int hal_audio_stop(hal_audio_dir_t dir)
{
    if (g_audio_ops == NULL || g_audio_ops->stop == NULL) {
        return HAL_ENODEV;
    }

    int ret = g_audio_ops->stop(dir);
    if (ret < 0) {
        driver_stats_record_err(&g_audio_stats, ret);
    } else {
        driver_stats_record_ok(&g_audio_stats);
    }
    return ret;
}

int hal_audio_read_block(void **buf, size_t *len, int timeout_ms)
{
    if (g_audio_ops == NULL || g_audio_ops->read_block == NULL) {
        return HAL_ENODEV;
    }

    int ret = g_audio_ops->read_block(buf, len, timeout_ms);
    if (ret < 0) {
        driver_stats_record_err(&g_audio_stats, ret);
    } else {
        driver_stats_record_ok(&g_audio_stats);
        audio_jitter_update(&g_audio_jitter,
                            k_cyc_to_us_near32(k_cycle_get_32()));
    }
    return ret;
}

int hal_audio_release(void *buf)
{
    if (g_audio_ops == NULL || g_audio_ops->release == NULL) {
        return HAL_ENODEV;
    }

    int ret = g_audio_ops->release(buf);
    if (ret < 0) {
        driver_stats_record_err(&g_audio_stats, ret);
    } else {
        driver_stats_record_ok(&g_audio_stats);
    }
    return ret;
}

int hal_audio_read(void *buf, size_t len, int timeout_ms)
{
    if (g_audio_ops == NULL || g_audio_ops->read == NULL) {
        return HAL_ENOTSUP;
    }

    int ret = g_audio_ops->read(buf, len, timeout_ms);
    if (ret < 0) {
        driver_stats_record_err(&g_audio_stats, ret);
    } else {
        driver_stats_record_ok(&g_audio_stats);
    }
    return ret;
}

int hal_audio_write(const void *buf, size_t len, int timeout_ms)
{
    if (g_audio_ops == NULL || g_audio_ops->write == NULL) {
        return HAL_ENODEV;
    }

    int ret = g_audio_ops->write(buf, len, timeout_ms);
    if (ret < 0) {
        driver_stats_record_err(&g_audio_stats, ret);
    } else {
        driver_stats_record_ok(&g_audio_stats);
    }
    return ret;
}

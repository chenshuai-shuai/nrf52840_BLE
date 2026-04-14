#include "hal_imu.h"
#include "error.h"

static const hal_imu_ops_t *g_imu_ops;

int hal_imu_register(const hal_imu_ops_t *ops)
{
    if (ops == NULL || ops->init == NULL) {
        return HAL_EINVAL;
    }
    if (g_imu_ops != NULL) {
        return HAL_EBUSY;
    }
    g_imu_ops = ops;
    return HAL_OK;
}

int hal_imu_init(void)
{
    if (g_imu_ops == NULL || g_imu_ops->init == NULL) {
        return HAL_ENODEV;
    }
    return g_imu_ops->init();
}

int hal_imu_read(void *buf, size_t len, int timeout_ms)
{
    if (g_imu_ops == NULL || g_imu_ops->read == NULL) {
        return HAL_ENODEV;
    }
    return g_imu_ops->read(buf, len, timeout_ms);
}

int hal_imu_read_timed(void *buf, size_t len, uint64_t *timestamp_us, int timeout_ms)
{
    if (g_imu_ops == NULL || g_imu_ops->read_timed == NULL) {
        return HAL_ENOTSUP;
    }
    return g_imu_ops->read_timed(buf, len, timestamp_us, timeout_ms);
}

int hal_imu_get_latest(imu_sample_t *out, uint32_t *timestamp_ms)
{
    if (g_imu_ops == NULL || g_imu_ops->get_latest == NULL) {
        return HAL_ENODEV;
    }
    return g_imu_ops->get_latest(out, timestamp_ms);
}

int hal_imu_get_latest_us(imu_sample_t *out, uint64_t *timestamp_us)
{
    if (g_imu_ops == NULL || g_imu_ops->get_latest_us == NULL) {
        return HAL_ENOTSUP;
    }
    return g_imu_ops->get_latest_us(out, timestamp_us);
}

int hal_imu_cache_start(void)
{
    if (g_imu_ops == NULL || g_imu_ops->cache_start == NULL) {
        return HAL_ENOTSUP;
    }
    return g_imu_ops->cache_start();
}

int hal_imu_cache_stop(void)
{
    if (g_imu_ops == NULL || g_imu_ops->cache_stop == NULL) {
        return HAL_ENOTSUP;
    }
    return g_imu_ops->cache_stop();
}

int hal_imu_cache_read(imu_sample_t *samples,
                       uint64_t *timestamps_us,
                       size_t *inout_count)
{
    if (g_imu_ops == NULL || g_imu_ops->cache_read == NULL) {
        return HAL_ENOTSUP;
    }
    return g_imu_ops->cache_read(samples, timestamps_us, inout_count);
}

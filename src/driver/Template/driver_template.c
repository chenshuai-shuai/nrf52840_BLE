#include <zephyr/kernel.h>

#include "driver_template.h"
#include "driver_stats.h"

/*
 * Driver Template (for reuse)
 *
 * Pattern:
 * - static state struct
 * - fixed memory pools for real-time paths
 * - ops table registered into HAL
 *
 * NOTE:
 * - This file is a template and is not compiled into the build.
 * - Copy and rename before use.
 */

struct driver_template_state {
    bool active;
    DRIVER_STATS_DEFINE(stats);
};

static struct driver_template_state g_state;

static int template_init(void)
{
    driver_stats_init(&g_state.stats);
    driver_stats_record_ok(&g_state.stats);
    return HAL_OK;
}

static int template_start(void)
{
    if (g_state.active) {
        driver_stats_record_err(&g_state.stats, HAL_EBUSY);
        return HAL_EBUSY;
    }

    g_state.active = true;
    driver_stats_record_ok(&g_state.stats);
    return HAL_OK;
}

static int template_stop(void)
{
    if (!g_state.active) {
        driver_stats_record_err(&g_state.stats, HAL_EBUSY);
        return HAL_EBUSY;
    }

    g_state.active = false;
    driver_stats_record_ok(&g_state.stats);
    return HAL_OK;
}

/* Optional read/write placeholders */
static int template_read(void *buf, size_t len, int timeout_ms)
{
    ARG_UNUSED(buf);
    ARG_UNUSED(len);
    ARG_UNUSED(timeout_ms);

    driver_stats_record_err(&g_state.stats, HAL_ENOTSUP);
    return HAL_ENOTSUP;
}

static int template_write(const void *buf, size_t len, int timeout_ms)
{
    ARG_UNUSED(buf);
    ARG_UNUSED(len);
    ARG_UNUSED(timeout_ms);

    driver_stats_record_err(&g_state.stats, HAL_ENOTSUP);
    return HAL_ENOTSUP;
}

/*
 * TODO:
 * - create hal_xxx_ops_t and register here
 * - implement read/write or zero-copy flow
 */

int driver_template_register(void)
{
    return HAL_ENOTSUP;
}

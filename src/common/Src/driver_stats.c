#include "driver_stats.h"

void driver_stats_init(driver_stats_t *s)
{
    if (s == NULL) {
        return;
    }

    atomic_clear(&s->ok_count);
    atomic_clear(&s->err_count);
    atomic_clear(&s->drop_count);
    atomic_clear(&s->overrun_count);
    atomic_set(&s->last_err, 0);
    atomic_set(&s->last_ts_ms, (atomic_val_t)k_uptime_get_32());
}

void driver_stats_record_ok(driver_stats_t *s)
{
    if (s == NULL) {
        return;
    }

    atomic_inc(&s->ok_count);
    atomic_set(&s->last_ts_ms, (atomic_val_t)k_uptime_get_32());
}

void driver_stats_record_err(driver_stats_t *s, int err)
{
    if (s == NULL) {
        return;
    }

    atomic_inc(&s->err_count);
    atomic_set(&s->last_err, (atomic_val_t)err);
    atomic_set(&s->last_ts_ms, (atomic_val_t)k_uptime_get_32());
}

void driver_stats_record_drop(driver_stats_t *s, uint32_t count)
{
    if (s == NULL) {
        return;
    }

    atomic_add(&s->drop_count, (atomic_val_t)count);
    atomic_set(&s->last_ts_ms, (atomic_val_t)k_uptime_get_32());
}

void driver_stats_record_overrun(driver_stats_t *s, uint32_t count)
{
    if (s == NULL) {
        return;
    }

    atomic_add(&s->overrun_count, (atomic_val_t)count);
    atomic_set(&s->last_ts_ms, (atomic_val_t)k_uptime_get_32());
}

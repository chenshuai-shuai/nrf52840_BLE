#ifndef COMMON_DRIVER_STATS_H
#define COMMON_DRIVER_STATS_H

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    atomic_t ok_count;
    atomic_t err_count;
    atomic_t drop_count;
    atomic_t overrun_count;
    atomic_t last_err;
    atomic_t last_ts_ms;
} driver_stats_t;

void driver_stats_init(driver_stats_t *s);
void driver_stats_record_ok(driver_stats_t *s);
void driver_stats_record_err(driver_stats_t *s, int err);
void driver_stats_record_drop(driver_stats_t *s, uint32_t count);
void driver_stats_record_overrun(driver_stats_t *s, uint32_t count);

#define DRIVER_STATS_DEFINE(name) driver_stats_t name

#ifdef __cplusplus
}
#endif

#endif /* COMMON_DRIVER_STATS_H */

#ifndef COMMON_RT_THREAD_H
#define COMMON_RT_THREAD_H

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RT_THREAD_STACK_DEFINE(name, size) K_THREAD_STACK_DEFINE(name, size)

int rt_thread_start(struct k_thread *thread,
                    k_thread_stack_t *stack,
                    size_t stack_size,
                    k_thread_entry_t entry,
                    void *p1, void *p2, void *p3,
                    int prio, uint32_t options,
                    const char *name);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_RT_THREAD_H */

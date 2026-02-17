#include <errno.h>

#include "rt_thread.h"

int rt_thread_start(struct k_thread *thread,
                    k_thread_stack_t *stack,
                    size_t stack_size,
                    k_thread_entry_t entry,
                    void *p1, void *p2, void *p3,
                    int prio, uint32_t options,
                    const char *name)
{
    if (thread == NULL || stack == NULL || entry == NULL || stack_size == 0U) {
        return -EINVAL;
    }

    k_tid_t tid = k_thread_create(thread, stack, stack_size, entry, p1, p2, p3,
                                  prio, options, K_NO_WAIT);
    if (name != NULL) {
        k_thread_name_set(tid, name);
    }

    return 0;
}

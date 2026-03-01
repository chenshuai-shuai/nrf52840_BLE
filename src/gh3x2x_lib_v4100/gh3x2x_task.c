#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include "gh3x2x_demo_config.h"
#include "gh3x2x_demo.h"

const k_tid_t gh3x2x_tid;
// struct k_sem gh3x2x_int_sem;
extern GU8 g_uchGh3x2xIntCallBackIsCalled;

void gh3x2x_task_entry(void)
{

    // k_sem_init(&gh3x2x_int_sem, 0, 1);

    Gh3x2xDemoInit();
    
    Gh3x2xDemoStartSampling(GH3X2X_FUNCTION_HR);
    while (1)
    {
        // if(k_sem_take(&gh3x2x_int_sem, K_MSEC(10)) == 0)
        if(1 == g_uchGh3x2xIntCallBackIsCalled)
        {
            Gh3x2xDemoInterruptProcess();
        }
    }
    
}

K_THREAD_DEFINE(gh3x2x_tid, 1024 * 4,
                gh3x2x_task_entry, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);


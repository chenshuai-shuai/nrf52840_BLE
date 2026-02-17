#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "platform_init.h"
#include "hal_audio.h"
#include "error.h"

int main(void)
{
    int ret = platform_init();
    if (ret != HAL_OK) {
        printk("platform_init failed: %d\n", ret);
        return ret;
    }

    ret = hal_audio_init();
    if (ret != HAL_OK) {
        printk("hal_audio_init failed: %d\n", ret);
        return ret;
    }

    return 0;
}

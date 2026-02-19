#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "platform_init.h"
#include "hal_audio.h"
#include "error.h"

#ifdef CONFIG_BLE_TEST
#include <zephyr/logging/log.h>
#include "hal_ble.h"
#include "rt_thread.h"

LOG_MODULE_REGISTER(ble_test, LOG_LEVEL_INF);

#define BLE_TEST_STACK_SIZE 2048
#define BLE_TEST_PRIO 3

static struct k_thread g_ble_test_thread;
RT_THREAD_STACK_DEFINE(g_ble_test_stack, BLE_TEST_STACK_SIZE);

static void ble_test_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    int ret = hal_ble_init();
    if (ret != HAL_OK) {
        LOG_ERR("hal_ble_init failed: %d", ret);
        return;
    }

    ret = hal_ble_start();
    if (ret != HAL_OK) {
        LOG_ERR("hal_ble_start failed: %d", ret);
        return;
    }

    LOG_INF("BLE test thread started");

    while (1) {
        uint8_t buf[244];
        int r = hal_ble_recv(buf, sizeof(buf), 1000);
        if (r > 0) {
            LOG_INF("BLE RX %d bytes", r);
            int s = hal_ble_send(buf, (size_t)r, 0);
            if (s != HAL_OK) {
                LOG_ERR("BLE TX failed: %d", s);
            }
        }
    }
}
#endif

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

#ifdef CONFIG_BLE_TEST
    (void)rt_thread_start(&g_ble_test_thread,
                          g_ble_test_stack,
                          K_THREAD_STACK_SIZEOF(g_ble_test_stack),
                          ble_test_entry,
                          NULL, NULL, NULL,
                          BLE_TEST_PRIO,
                          0,
                          "ble_test");
#endif

    return 0;
}

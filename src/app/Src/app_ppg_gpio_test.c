#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <hal/nrf_gpio.h>

#include "app_ppg_gpio_test.h"

#define PPG_GPIO_TEST_STACK_SIZE 1536
#define PPG_GPIO_TEST_PRIORITY   7

#define PPG_CS_PIN   12
#define PPG_RST_PIN  6
#define PPG_INT_PIN  7

static struct k_thread g_ppg_gpio_test_thread;
K_THREAD_STACK_DEFINE(g_ppg_gpio_test_stack, PPG_GPIO_TEST_STACK_SIZE);

static const struct device * const g_gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));

static void ppg_gpio_cfg_output_readback(uint32_t pin, int init_high)
{
    NRF_P0->PIN_CNF[pin] =
        ((uint32_t)GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos) |
        ((uint32_t)GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
        ((uint32_t)GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos) |
        ((uint32_t)GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos) |
        ((uint32_t)GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos);

    if (init_high) {
        NRF_P0->OUTSET = BIT(pin);
    } else {
        NRF_P0->OUTCLR = BIT(pin);
    }
}

static void ppg_gpio_dump_regs(const char *tag)
{
    uint32_t out = NRF_P0->OUT;
    uint32_t in = NRF_P0->IN;
    uint32_t dir = NRF_P0->DIR;
    uint32_t cs_cnf = NRF_P0->PIN_CNF[PPG_CS_PIN];
    uint32_t rst_cnf = NRF_P0->PIN_CNF[PPG_RST_PIN];
    uint32_t int_cnf = NRF_P0->PIN_CNF[PPG_INT_PIN];

    printk("PPG_REG[%s]: OUT=0x%08x IN=0x%08x DIR=0x%08x PIN_CNF(cs)=0x%08x PIN_CNF(rst)=0x%08x PIN_CNF(int)=0x%08x\n",
           tag,
           (unsigned int)out,
           (unsigned int)in,
           (unsigned int)dir,
           (unsigned int)cs_cnf,
           (unsigned int)rst_cnf,
           (unsigned int)int_cnf);
}

static void ppg_gpio_dump(const char *tag, int cs_set, int rst_set)
{
    int cs = gpio_pin_get(g_gpio0, PPG_CS_PIN);
    int rst = gpio_pin_get(g_gpio0, PPG_RST_PIN);
    int irq = gpio_pin_get(g_gpio0, PPG_INT_PIN);

    printk("PPG_GPIO[%s]: set(cs=%d rst=%d) read(cs=%d rst=%d int=%d)\n",
           tag,
           cs_set,
           rst_set,
           cs,
           rst,
           irq);
    ppg_gpio_dump_regs(tag);
}

static void app_ppg_gpio_test_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    if (!device_is_ready(g_gpio0)) {
        printk("PPG_GPIO: gpio0 not ready\n");
        return;
    }

    int ret_cs = gpio_pin_configure(g_gpio0, PPG_CS_PIN, GPIO_DISCONNECTED);
    int ret_rst = gpio_pin_configure(g_gpio0, PPG_RST_PIN, GPIO_DISCONNECTED);
    int ret_int = gpio_pin_configure(g_gpio0, PPG_INT_PIN, GPIO_INPUT | GPIO_PULL_UP);

    ppg_gpio_cfg_output_readback(PPG_CS_PIN, 1);
    ppg_gpio_cfg_output_readback(PPG_RST_PIN, 1);

    printk("PPG_GPIO: cfg cs(P0.12)=%d rst(P0.06)=%d int(P0.07)=%d\n",
           ret_cs,
           ret_rst,
           ret_int);
    ppg_gpio_dump("init", 1, 1);

    while (1) {
        int set_cs = gpio_pin_set(g_gpio0, PPG_CS_PIN, 1);
        int set_rst = gpio_pin_set(g_gpio0, PPG_RST_PIN, 0);
        printk("PPG_GPIO: wr phase=A cs=1(%d) rst=0(%d)\n", set_cs, set_rst);
        ppg_gpio_dump("A", 1, 0);
        k_sleep(K_MSEC(1000));

        set_cs = gpio_pin_set(g_gpio0, PPG_CS_PIN, 0);
        set_rst = gpio_pin_set(g_gpio0, PPG_RST_PIN, 1);
        printk("PPG_GPIO: wr phase=B cs=0(%d) rst=1(%d)\n", set_cs, set_rst);
        ppg_gpio_dump("B", 0, 1);
        k_sleep(K_MSEC(1000));
    }
}

void app_ppg_gpio_test_start(void)
{
    (void)k_thread_create(&g_ppg_gpio_test_thread,
                          g_ppg_gpio_test_stack,
                          K_THREAD_STACK_SIZEOF(g_ppg_gpio_test_stack),
                          app_ppg_gpio_test_thread_entry,
                          NULL, NULL, NULL,
                          PPG_GPIO_TEST_PRIORITY,
                          0,
                          K_NO_WAIT);
    k_thread_name_set(&g_ppg_gpio_test_thread, "ppg_gpio_test");
}

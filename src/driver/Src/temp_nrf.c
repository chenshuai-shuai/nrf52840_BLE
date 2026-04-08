#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "error.h"
#include "hal_temp.h"

#if IS_ENABLED(CONFIG_TEMP_TEST)
LOG_MODULE_REGISTER(temp_nrf, LOG_LEVEL_INF);
#else
LOG_MODULE_REGISTER(temp_nrf, LOG_LEVEL_WRN);
#endif

#define TMP119_NODE DT_NODELABEL(tmp119)

#if !DT_NODE_HAS_STATUS(TMP119_NODE, okay)
#error "tmp119 node is not enabled in devicetree"
#endif

enum {
    TMP119_REG_TEMP_RESULT = 0x00,
    TMP119_REG_CONFIG = 0x01,
    TMP119_REG_DEVICE_ID = 0x0F,
};

#define TMP119_DEVICE_ID          0x0117U
#define TMP119_DEVICE_ID_MASK     0x0FFFU
#define TMP119_CFG_DATA_READY     0x2000U
#define TMP119_CFG_SOFT_RESET     0x0002U
#define TMP119_CFG_CC_500MS_DRDY  0x0044U
#define TMP119_I2C_DELAY_US       5U
#define TMP119_STARTUP_DELAY_MS   5U
#define TMP119_RESET_DELAY_MS     10U
#define TMP119_READY_POLL_MS      1U
#define TMP119_DEFAULT_TIMEOUT_MS 1000

static const struct gpio_dt_spec g_scl = GPIO_DT_SPEC_GET(TMP119_NODE, scl_gpios);
static const struct gpio_dt_spec g_sda = GPIO_DT_SPEC_GET(TMP119_NODE, sda_gpios);
static const struct gpio_dt_spec g_alert = GPIO_DT_SPEC_GET(TMP119_NODE, alert_gpios);
static const uint8_t g_i2c_addr = DT_REG_ADDR(TMP119_NODE);

typedef struct {
    bool inited;
    bool sda_input;
    uint16_t device_id;
    struct k_mutex lock;
} tmp119_state_t;

static tmp119_state_t g_tmp119;

static void tmp119_delay(void)
{
    k_busy_wait(TMP119_I2C_DELAY_US);
}

static int tmp119_gpio_write(const struct gpio_dt_spec *spec, int value)
{
    int ret = gpio_pin_set_dt(spec, value);
    return (ret == 0) ? HAL_OK : ret;
}

static int tmp119_gpio_read(const struct gpio_dt_spec *spec)
{
    int ret = gpio_pin_get_dt(spec);
    if (ret < 0) {
        return ret;
    }
    return ret;
}

static int tmp119_sda_output(int value)
{
    if (g_tmp119.sda_input) {
        int ret = gpio_pin_configure_dt(&g_sda, GPIO_OUTPUT);
        if (ret != 0) {
            return ret;
        }
        g_tmp119.sda_input = false;
    }
    return tmp119_gpio_write(&g_sda, value ? 1 : 0);
}

static int tmp119_sda_input(void)
{
    if (!g_tmp119.sda_input) {
        int ret = gpio_pin_configure_dt(&g_sda, GPIO_INPUT | GPIO_PULL_UP);
        if (ret != 0) {
            return ret;
        }
        g_tmp119.sda_input = true;
    }
    return HAL_OK;
}

static void tmp119_bus_idle(void)
{
    (void)tmp119_sda_output(1);
    (void)tmp119_gpio_write(&g_scl, 1);
    tmp119_delay();
}

static void tmp119_start(void)
{
    (void)tmp119_sda_output(1);
    (void)tmp119_gpio_write(&g_scl, 1);
    tmp119_delay();
    (void)tmp119_sda_output(0);
    tmp119_delay();
    (void)tmp119_gpio_write(&g_scl, 0);
    tmp119_delay();
}

static void tmp119_stop(void)
{
    (void)tmp119_sda_output(0);
    tmp119_delay();
    (void)tmp119_gpio_write(&g_scl, 1);
    tmp119_delay();
    (void)tmp119_sda_output(1);
    tmp119_delay();
}

static int tmp119_write_bit(int bit)
{
    int ret = tmp119_sda_output(bit ? 1 : 0);
    if (ret != HAL_OK) {
        return ret;
    }
    tmp119_delay();
    ret = tmp119_gpio_write(&g_scl, 1);
    if (ret != HAL_OK) {
        return ret;
    }
    tmp119_delay();
    ret = tmp119_gpio_write(&g_scl, 0);
    if (ret != HAL_OK) {
        return ret;
    }
    tmp119_delay();
    return HAL_OK;
}

static int tmp119_read_bit(int *bit)
{
    if (bit == NULL) {
        return HAL_EINVAL;
    }

    int ret = tmp119_sda_input();
    if (ret != HAL_OK) {
        return ret;
    }
    tmp119_delay();
    ret = tmp119_gpio_write(&g_scl, 1);
    if (ret != HAL_OK) {
        return ret;
    }
    tmp119_delay();
    ret = tmp119_gpio_read(&g_sda);
    if (ret < 0) {
        (void)tmp119_gpio_write(&g_scl, 0);
        return ret;
    }
    *bit = ret ? 1 : 0;
    ret = tmp119_gpio_write(&g_scl, 0);
    if (ret != HAL_OK) {
        return ret;
    }
    tmp119_delay();
    return HAL_OK;
}

static int tmp119_write_byte(uint8_t value)
{
    for (int i = 0; i < 8; i++) {
        int ret = tmp119_write_bit((value & 0x80U) != 0U);
        if (ret != HAL_OK) {
            return ret;
        }
        value <<= 1;
    }

    int ack = 1;
    int ret = tmp119_read_bit(&ack);
    if (ret != HAL_OK) {
        return ret;
    }
    return (ack == 0) ? HAL_OK : HAL_EIO;
}

static int tmp119_read_byte(uint8_t *value, bool ack)
{
    if (value == NULL) {
        return HAL_EINVAL;
    }

    uint8_t data = 0U;
    for (int i = 0; i < 8; i++) {
        int bit = 0;
        int ret = tmp119_read_bit(&bit);
        if (ret != HAL_OK) {
            return ret;
        }
        data = (uint8_t)((data << 1) | (bit ? 1U : 0U));
    }

    int ret = tmp119_write_bit(ack ? 0 : 1);
    if (ret != HAL_OK) {
        return ret;
    }

    *value = data;
    return HAL_OK;
}

static int tmp119_write_register(uint8_t reg, uint16_t value)
{
    tmp119_start();

    int ret = tmp119_write_byte((uint8_t)(g_i2c_addr << 1));
    if (ret != HAL_OK) {
        tmp119_stop();
        return ret;
    }
    ret = tmp119_write_byte(reg);
    if (ret != HAL_OK) {
        tmp119_stop();
        return ret;
    }
    ret = tmp119_write_byte((uint8_t)(value >> 8));
    if (ret != HAL_OK) {
        tmp119_stop();
        return ret;
    }
    ret = tmp119_write_byte((uint8_t)(value & 0xFFU));
    tmp119_stop();
    return ret;
}

static int tmp119_read_register(uint8_t reg, uint16_t *value)
{
    if (value == NULL) {
        return HAL_EINVAL;
    }

    tmp119_start();

    int ret = tmp119_write_byte((uint8_t)(g_i2c_addr << 1));
    if (ret != HAL_OK) {
        tmp119_stop();
        return ret;
    }
    ret = tmp119_write_byte(reg);
    if (ret != HAL_OK) {
        tmp119_stop();
        return ret;
    }

    tmp119_start();
    ret = tmp119_write_byte((uint8_t)((g_i2c_addr << 1) | 0x01U));
    if (ret != HAL_OK) {
        tmp119_stop();
        return ret;
    }

    uint8_t msb = 0U;
    uint8_t lsb = 0U;
    ret = tmp119_read_byte(&msb, true);
    if (ret != HAL_OK) {
        tmp119_stop();
        return ret;
    }
    ret = tmp119_read_byte(&lsb, false);
    tmp119_stop();
    if (ret != HAL_OK) {
        return ret;
    }

    *value = (uint16_t)(((uint16_t)msb << 8) | lsb);
    return HAL_OK;
}

static int tmp119_wait_ready_locked(int timeout_ms)
{
    int remaining = (timeout_ms < 0) ? -1 : timeout_ms;

    while (1) {
        uint16_t cfg = 0U;
        int ret = tmp119_read_register(TMP119_REG_CONFIG, &cfg);
        if (ret == HAL_OK && (cfg & TMP119_CFG_DATA_READY) != 0U) {
            return HAL_OK;
        }

        int level = tmp119_gpio_read(&g_alert);
        if (level < 0) {
            return level;
        }
        if (level == 0) {
            return HAL_OK;
        }
        if (remaining == 0) {
            return HAL_EBUSY;
        }
        k_msleep(TMP119_READY_POLL_MS);
        if (remaining > 0) {
            remaining -= TMP119_READY_POLL_MS;
            if (remaining < 0) {
                remaining = 0;
            }
        }
    }
}

static int32_t tmp119_raw_to_micro_c(int16_t raw)
{
    int64_t scaled = (int64_t)raw * 78125LL;
    if (scaled >= 0) {
        return (int32_t)((scaled + 5LL) / 10LL);
    }
    return (int32_t)((scaled - 5LL) / 10LL);
}

static int tmp119_nrf_init(void)
{
    if (g_tmp119.inited) {
        return HAL_OK;
    }

    if (!device_is_ready(g_scl.port) ||
        !device_is_ready(g_sda.port) ||
        !device_is_ready(g_alert.port)) {
        LOG_ERR("tmp119 gpio not ready: scl=%d sda=%d alert=%d",
                device_is_ready(g_scl.port) ? 1 : 0,
                device_is_ready(g_sda.port) ? 1 : 0,
                device_is_ready(g_alert.port) ? 1 : 0);
        return HAL_ENODEV;
    }

    int ret = gpio_pin_configure_dt(&g_scl, GPIO_OUTPUT_HIGH);
    if (ret != 0) {
        LOG_ERR("tmp119 scl gpio configure failed: %d", ret);
        return ret;
    }
    ret = gpio_pin_configure_dt(&g_sda, GPIO_OUTPUT_HIGH);
    if (ret != 0) {
        LOG_ERR("tmp119 sda gpio configure failed: %d", ret);
        return ret;
    }
    ret = gpio_pin_configure_dt(&g_alert, GPIO_INPUT | GPIO_PULL_UP);
    if (ret != 0) {
        LOG_ERR("tmp119 alert gpio configure failed: %d", ret);
        return ret;
    }

    k_mutex_init(&g_tmp119.lock);
    g_tmp119.sda_input = false;
    tmp119_bus_idle();
    k_msleep(TMP119_STARTUP_DELAY_MS);

    k_mutex_lock(&g_tmp119.lock, K_FOREVER);

    ret = tmp119_write_register(TMP119_REG_CONFIG, TMP119_CFG_SOFT_RESET);
    if (ret != HAL_OK) {
        LOG_ERR("tmp119 soft reset write failed: %d", ret);
    }
    if (ret == HAL_OK) {
        k_msleep(TMP119_RESET_DELAY_MS);
        ret = tmp119_read_register(TMP119_REG_DEVICE_ID, &g_tmp119.device_id);
        if (ret != HAL_OK) {
            LOG_ERR("tmp119 device id read failed: %d", ret);
        }
    }
    if (ret == HAL_OK &&
        (g_tmp119.device_id & TMP119_DEVICE_ID_MASK) != TMP119_DEVICE_ID) {
        LOG_ERR("tmp119 device id mismatch: got=0x%04x expect=0x%04x mask=0x%04x",
                g_tmp119.device_id, TMP119_DEVICE_ID, TMP119_DEVICE_ID_MASK);
        ret = HAL_ENODEV;
    }
    if (ret == HAL_OK) {
        ret = tmp119_write_register(TMP119_REG_CONFIG, TMP119_CFG_CC_500MS_DRDY);
        if (ret != HAL_OK) {
            LOG_ERR("tmp119 config write failed: %d", ret);
        }
    }
    if (ret == HAL_OK) {
        g_tmp119.inited = true;
        LOG_INF("tmp119 init: addr=0x%02x id=0x%04x", g_i2c_addr, g_tmp119.device_id);
    }

    k_mutex_unlock(&g_tmp119.lock);
    return ret;
}

static int tmp119_nrf_read(hal_temp_sample_t *out, int timeout_ms)
{
    if (out == NULL) {
        return HAL_EINVAL;
    }
    if (!g_tmp119.inited) {
        return HAL_ENODEV;
    }

    if (timeout_ms == 0) {
        timeout_ms = TMP119_DEFAULT_TIMEOUT_MS;
    }

    k_mutex_lock(&g_tmp119.lock, K_FOREVER);

    int ret = tmp119_wait_ready_locked(timeout_ms);
    if (ret != HAL_OK) {
        k_mutex_unlock(&g_tmp119.lock);
        return ret;
    }

    uint16_t reg = 0U;
    ret = tmp119_read_register(TMP119_REG_TEMP_RESULT, &reg);
    if (ret == HAL_OK) {
        /* 0x8000 is TMP119's invalid/default result immediately after reset. */
        if (reg == 0x8000U) {
            k_mutex_unlock(&g_tmp119.lock);
            return HAL_EBUSY;
        }
        int16_t raw = (int16_t)reg;
        out->raw = raw;
        out->micro_c = tmp119_raw_to_micro_c(raw);
        out->celsius = (float)raw * 0.0078125f;
        out->timestamp_ms = (uint32_t)k_uptime_get();
    }

    k_mutex_unlock(&g_tmp119.lock);
    return ret;
}

static int tmp119_nrf_get_device_id(uint16_t *out)
{
    if (out == NULL) {
        return HAL_EINVAL;
    }
    if (!g_tmp119.inited) {
        return HAL_ENODEV;
    }

    *out = g_tmp119.device_id;
    return HAL_OK;
}

static const hal_temp_ops_t g_temp_ops = {
    .init = tmp119_nrf_init,
    .read = tmp119_nrf_read,
    .get_device_id = tmp119_nrf_get_device_id,
};

int temp_nrf_register(void)
{
    return hal_temp_register(&g_temp_ops);
}

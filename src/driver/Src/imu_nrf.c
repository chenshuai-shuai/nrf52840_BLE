#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <string.h>

#include "hal_imu.h"
#include "error.h"
#include "spi_bus_arbiter.h"

LOG_MODULE_REGISTER(imu_nrf, LOG_LEVEL_WRN);

#define IMU_NODE DT_NODELABEL(imu)
#if !DT_NODE_HAS_STATUS(IMU_NODE, okay)
#error "IMU devicetree node is not enabled"
#endif

#define IMU_INT_NODE DT_ALIAS(imu_int)
#if !DT_NODE_HAS_STATUS(IMU_INT_NODE, okay)
#error "imu_int alias is not defined in devicetree"
#endif

#define IMU_SPI_READ_FLAG 0x80U

#define REG_DEVICE_CONFIG 0x11U
#define REG_DRIVE_CONFIG  0x13U
#define REG_INT_CONFIG    0x14U
#define REG_TEMP_DATA1    0x1DU
#define REG_PWR_MGMT0     0x4EU
#define REG_GYRO_CONFIG0  0x4FU
#define REG_ACCEL_CONFIG0 0x50U
#define REG_INT_CONFIG1   0x64U
#define REG_INT_SOURCE0   0x65U
#define REG_WHO_AM_I      0x75U

#define IMU_WHO_AM_I_VAL  0x47U

#define IMU_MAX_READ_LEN  16U

static const struct spi_dt_spec g_imu_spi =
    SPI_DT_SPEC_GET(IMU_NODE,
                    SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
                    0);

static const struct gpio_dt_spec g_imu_int =
    GPIO_DT_SPEC_GET(IMU_INT_NODE, gpios);

static struct {
    bool inited;
    atomic_t irq_count;
    struct spi_config cfg;
    bool cfg_inited;
    struct k_mutex latest_lock;
    imu_sample_t latest;
    uint32_t latest_ts_ms;
    bool latest_valid;
} g_imu;

K_SEM_DEFINE(g_imu_drdy_sem, 0, 1);
static struct gpio_callback g_imu_int_cb;

static void imu_int_handler(const struct device *dev,
                            struct gpio_callback *cb,
                            uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    atomic_inc(&g_imu.irq_count);
    k_sem_give(&g_imu_drdy_sem);
}

static int imu_reg_write_cfg(const struct spi_config *cfg, uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { (uint8_t)(reg & 0x7FU), val };
    const struct spi_buf buf = {
        .buf = tx,
        .len = sizeof(tx),
    };
    const struct spi_buf_set tx_set = {
        .buffers = &buf,
        .count = 1,
    };
    int ret = spi_bus_lock(SPI_BUS_CLIENT_IMU, K_MSEC(20));
    if (ret) {
        return ret;
    }
    ret = spi_write(g_imu_spi.bus, cfg, &tx_set);
    (void)spi_bus_unlock(SPI_BUS_CLIENT_IMU);
    return ret;
}

static int imu_reg_read_block_cfg(const struct spi_config *cfg,
                                  uint8_t reg, uint8_t *buf, size_t len)
{
    if (buf == NULL || len == 0U || len > IMU_MAX_READ_LEN) {
        return HAL_EINVAL;
    }

    uint8_t tx_cmd[1] = { (uint8_t)(reg | IMU_SPI_READ_FLAG) };
    uint8_t dummy[IMU_MAX_READ_LEN] = {0};

    struct spi_buf tx_bufs[2] = {
        { .buf = tx_cmd, .len = sizeof(tx_cmd) },
        { .buf = dummy, .len = len },
    };
    struct spi_buf rx_bufs[2] = {
        { .buf = NULL, .len = sizeof(tx_cmd) },
        { .buf = buf, .len = len },
    };
    const struct spi_buf_set tx_set = {
        .buffers = tx_bufs,
        .count = 2,
    };
    const struct spi_buf_set rx_set = {
        .buffers = rx_bufs,
        .count = 2,
    };
    int ret = spi_bus_lock(SPI_BUS_CLIENT_IMU, K_MSEC(20));
    if (ret) {
        return ret;
    }
    ret = spi_transceive(g_imu_spi.bus, cfg, &tx_set, &rx_set);
    (void)spi_bus_unlock(SPI_BUS_CLIENT_IMU);
    return ret;
}

static int imu_reg_read_cfg(const struct spi_config *cfg, uint8_t reg, uint8_t *val)
{
    return imu_reg_read_block_cfg(cfg, reg, val, 1U);
}

static int imu_gpio_init(void)
{
    if (!device_is_ready(g_imu_int.port)) {
        LOG_ERR("imu int gpio not ready");
        return HAL_ENODEV;
    }

    int rc = gpio_pin_configure_dt(&g_imu_int, GPIO_INPUT | GPIO_PULL_DOWN);
    if (rc) {
        LOG_ERR("imu int gpio configure failed: %d", rc);
        return rc;
    }

    rc = gpio_pin_interrupt_configure_dt(&g_imu_int, GPIO_INT_EDGE_TO_ACTIVE);
    if (rc) {
        LOG_ERR("imu int interrupt config failed: %d", rc);
        return rc;
    }

    gpio_init_callback(&g_imu_int_cb, imu_int_handler, BIT(g_imu_int.pin));
    rc = gpio_add_callback(g_imu_int.port, &g_imu_int_cb);
    if (rc) {
        LOG_ERR("imu int callback add failed: %d", rc);
        return rc;
    }

    return HAL_OK;
}

static int imu_configure(void)
{
    int rc;

    rc = imu_reg_write_cfg(&g_imu.cfg, REG_DEVICE_CONFIG, 0x01);
    if (rc) {
        LOG_ERR("imu reset failed: %d", rc);
        return rc;
    }
    k_msleep(5);

    uint8_t who = 0;
    rc = imu_reg_read_cfg(&g_imu.cfg, REG_WHO_AM_I, &who);
    if (rc) {
        LOG_ERR("imu whoami read failed: %d", rc);
        return rc;
    }
    LOG_INF("imu whoami=0x%02x (op=0x%x freq=%u)",
            who, (unsigned)g_imu.cfg.operation,
            (unsigned)g_imu.cfg.frequency);
    if (who != IMU_WHO_AM_I_VAL) {
        struct spi_config alt = g_imu.cfg;
        alt.operation |= (SPI_MODE_CPHA | SPI_MODE_CPOL);
        alt.frequency = 1000000U;
        uint8_t who2 = 0;
        int rc2 = imu_reg_read_cfg(&alt, REG_WHO_AM_I, &who2);
        LOG_INF("imu whoami=0x%02x (op=0x%x freq=%u)",
                who2, (unsigned)alt.operation, (unsigned)alt.frequency);
        if (rc2 == HAL_OK && who2 == IMU_WHO_AM_I_VAL) {
            g_imu.cfg = alt;
            LOG_WRN("imu whoami ok with SPI mode3 @1MHz, switching config");
        } else {
            LOG_ERR("imu whoami mismatch (expect 0x%02x)", IMU_WHO_AM_I_VAL);
            return HAL_EIO;
        }
    }

    rc = imu_reg_write_cfg(&g_imu.cfg, REG_DRIVE_CONFIG, 0x05);
    if (rc) {
        LOG_ERR("imu drive config failed: %d", rc);
        return rc;
    }

    rc = imu_reg_write_cfg(&g_imu.cfg, REG_INT_CONFIG, 0x03);
    if (rc) {
        LOG_ERR("imu int config failed: %d", rc);
        return rc;
    }

    rc = imu_reg_write_cfg(&g_imu.cfg, REG_INT_CONFIG1, 0x00);
    if (rc) {
        LOG_ERR("imu int config1 failed: %d", rc);
        return rc;
    }

    rc = imu_reg_write_cfg(&g_imu.cfg, REG_INT_SOURCE0, 0x08);
    if (rc) {
        LOG_ERR("imu int source0 failed: %d", rc);
        return rc;
    }

    /* FS=2000dps, ODR=100Hz */
    rc = imu_reg_write_cfg(&g_imu.cfg, REG_GYRO_CONFIG0, 0x08);
    if (rc) {
        LOG_ERR("imu gyro config0 failed: %d", rc);
        return rc;
    }

    /* FS=16g, ODR=100Hz */
    rc = imu_reg_write_cfg(&g_imu.cfg, REG_ACCEL_CONFIG0, 0x08);
    if (rc) {
        LOG_ERR("imu accel config0 failed: %d", rc);
        return rc;
    }

    rc = imu_reg_write_cfg(&g_imu.cfg, REG_PWR_MGMT0, 0x0F);
    if (rc) {
        LOG_ERR("imu pwr mgmt failed: %d", rc);
        return rc;
    }
    k_busy_wait(300);
    k_msleep(50);

    return HAL_OK;
}

static int imu_nrf_init(void)
{
    if (g_imu.inited) {
        return HAL_OK;
    }

    if (!device_is_ready(g_imu_spi.bus)) {
        LOG_ERR("imu spi bus not ready");
        return HAL_ENODEV;
    }

    if (!g_imu.cfg_inited) {
        g_imu.cfg = g_imu_spi.config;
        g_imu.cfg_inited = true;
    }

    int rc = imu_gpio_init();
    if (rc != HAL_OK) {
        return rc;
    }

    rc = imu_configure();
    if (rc != HAL_OK) {
        return rc;
    }

    g_imu.inited = true;
    LOG_INF("imu init done (LN, 100Hz, accel +/-16g, gyro +/-2000dps)");
    return HAL_OK;
}

static int imu_nrf_read(void *buf, size_t len, int timeout_ms)
{
    if (!g_imu.inited) {
        return HAL_ENODEV;
    }
    if (buf == NULL || len < sizeof(imu_sample_t)) {
        return HAL_EINVAL;
    }

    k_timeout_t timeout = (timeout_ms < 0) ? K_FOREVER : K_MSEC(timeout_ms);
    int rc = k_sem_take(&g_imu_drdy_sem, timeout);
    if (rc) {
        return (rc == -EAGAIN) ? HAL_EBUSY : rc;
    }

    uint8_t raw[14] = {0};
    rc = imu_reg_read_block_cfg(&g_imu.cfg, REG_TEMP_DATA1, raw, sizeof(raw));
    if (rc) {
        return rc;
    }

    imu_sample_t sample = {
        .temp = (int16_t)((raw[0] << 8) | raw[1]),
        .accel_x = (int16_t)((raw[2] << 8) | raw[3]),
        .accel_y = (int16_t)((raw[4] << 8) | raw[5]),
        .accel_z = (int16_t)((raw[6] << 8) | raw[7]),
        .gyro_x = (int16_t)((raw[8] << 8) | raw[9]),
        .gyro_y = (int16_t)((raw[10] << 8) | raw[11]),
        .gyro_z = (int16_t)((raw[12] << 8) | raw[13]),
    };

    k_mutex_lock(&g_imu.latest_lock, K_FOREVER);
    g_imu.latest = sample;
    g_imu.latest_ts_ms = (uint32_t)k_uptime_get();
    g_imu.latest_valid = true;
    k_mutex_unlock(&g_imu.latest_lock);

    *(imu_sample_t *)buf = sample;

    return HAL_OK;
}

static int imu_nrf_get_latest(imu_sample_t *out, uint32_t *timestamp_ms)
{
    if (out == NULL) {
        return HAL_EINVAL;
    }
    k_mutex_lock(&g_imu.latest_lock, K_FOREVER);
    if (!g_imu.latest_valid) {
        k_mutex_unlock(&g_imu.latest_lock);
        return HAL_ENODEV;
    }
    *out = g_imu.latest;
    if (timestamp_ms != NULL) {
        *timestamp_ms = g_imu.latest_ts_ms;
    }
    k_mutex_unlock(&g_imu.latest_lock);
    return HAL_OK;
}

static const hal_imu_ops_t g_imu_ops = {
    .init = imu_nrf_init,
    .read = imu_nrf_read,
    .get_latest = imu_nrf_get_latest,
};

int imu_nrf_register(void)
{
    static bool lock_inited;
    if (!lock_inited) {
        k_mutex_init(&g_imu.latest_lock);
        lock_inited = true;
    }
    return hal_imu_register(&g_imu_ops);
}

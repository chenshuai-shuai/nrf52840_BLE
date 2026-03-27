#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <string.h>

#define ICM42688P 1
#include "Invn/Drivers/Icm426xx/Icm426xxDefs.h"
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

#define REG_DEVICE_CONFIG      0x11U
#define REG_DRIVE_CONFIG       0x13U
#define REG_INT_CONFIG         0x14U
#define REG_FIFO_CONFIG        0x16U
#define REG_TEMP_DATA1         0x1DU
#define REG_FIFO_COUNTH        0x2EU
#define REG_FIFO_DATA          0x30U
#define REG_SIGNAL_PATH_RESET  0x4BU
#define REG_INTF_CONFIG0       0x4CU
#define REG_PWR_MGMT0          0x4EU
#define REG_GYRO_CONFIG0       0x4FU
#define REG_ACCEL_CONFIG0      0x50U
#define REG_TMST_CONFIG        0x54U
#define REG_FIFO_CONFIG1       0x5FU
#define REG_FIFO_CONFIG2       0x60U
#define REG_INT_CONFIG1        0x64U
#define REG_INT_SOURCE0        0x65U
#define REG_WHO_AM_I           0x75U

#define IMU_WHO_AM_I_VAL       0x47U

#define IMU_MAX_READ_LEN       128U
#define IMU_IRQ_TS_RING_CAP    32U
#define IMU_FIFO_PACKET_SIZE   FIFO_16BYTES_PACKET_SIZE
#define IMU_FIFO_BATCH_PACKETS 8U
#define IMU_SAMPLE_PERIOD_US   10000U

typedef struct {
    imu_sample_t sample;
    uint64_t ts_us;
} imu_fifo_sample_t;

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
    uint64_t latest_ts_us;
    bool latest_valid;
    uint8_t data_endian;
    bool fifo_enabled;
    uint64_t irq_ts_us[IMU_IRQ_TS_RING_CAP];
    uint8_t irq_ts_head;
    uint8_t irq_ts_tail;
    uint8_t irq_ts_count;
    imu_fifo_sample_t fifo_ring[CONFIG_IMU_FIFO_SAMPLE_RING_CAP];
    uint8_t fifo_ridx;
    uint8_t fifo_widx;
    uint8_t fifo_count;
} g_imu;

K_SEM_DEFINE(g_imu_drdy_sem, 0, 1);
static struct gpio_callback g_imu_int_cb;

static int imu_reg_write_cfg(const struct spi_config *cfg, uint8_t reg, uint8_t val);
static int imu_reg_read_block_cfg(const struct spi_config *cfg, uint8_t reg, uint8_t *buf, size_t len);
static int imu_reg_read_cfg(const struct spi_config *cfg, uint8_t reg, uint8_t *val);

static void imu_store_latest(const imu_sample_t *sample, uint64_t ts_us)
{
    if (sample == NULL) {
        return;
    }

    k_mutex_lock(&g_imu.latest_lock, K_FOREVER);
    g_imu.latest = *sample;
    g_imu.latest_ts_us = ts_us;
    g_imu.latest_ts_ms = (uint32_t)(ts_us / 1000U);
    g_imu.latest_valid = true;
    k_mutex_unlock(&g_imu.latest_lock);
}

static uint16_t imu_parse_u16(const uint8_t *buf)
{
    if (g_imu.data_endian == ICM426XX_INTF_CONFIG0_DATA_LITTLE_ENDIAN) {
        return (uint16_t)(((uint16_t)buf[1] << 8) | buf[0]);
    }
    return (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
}

static void imu_int_handler(const struct device *dev,
                            struct gpio_callback *cb,
                            uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    uint64_t timestamp_us = k_cyc_to_us_floor64(k_cycle_get_64());
    unsigned int key = irq_lock();

    if (g_imu.irq_ts_count == IMU_IRQ_TS_RING_CAP) {
        g_imu.irq_ts_tail = (uint8_t)((g_imu.irq_ts_tail + 1U) % IMU_IRQ_TS_RING_CAP);
        g_imu.irq_ts_count--;
    }
    g_imu.irq_ts_us[g_imu.irq_ts_head] = timestamp_us;
    g_imu.irq_ts_head = (uint8_t)((g_imu.irq_ts_head + 1U) % IMU_IRQ_TS_RING_CAP);
    g_imu.irq_ts_count++;
    irq_unlock(key);

    atomic_inc(&g_imu.irq_count);
    k_sem_give(&g_imu_drdy_sem);
}

static uint64_t imu_pop_irq_timestamps(uint8_t requested_count)
{
    uint64_t latest_ts_us = 0U;
    unsigned int key = irq_lock();

    while ((requested_count > 0U) && (g_imu.irq_ts_count > 0U)) {
        latest_ts_us = g_imu.irq_ts_us[g_imu.irq_ts_tail];
        g_imu.irq_ts_tail = (uint8_t)((g_imu.irq_ts_tail + 1U) % IMU_IRQ_TS_RING_CAP);
        g_imu.irq_ts_count--;
        requested_count--;
    }

    irq_unlock(key);
    return latest_ts_us;
}

static void imu_fifo_ring_push(const imu_sample_t *sample, uint64_t ts_us)
{
    if (sample == NULL) {
        return;
    }

    if (g_imu.fifo_count == CONFIG_IMU_FIFO_SAMPLE_RING_CAP) {
        g_imu.fifo_ridx = (uint8_t)((g_imu.fifo_ridx + 1U) % CONFIG_IMU_FIFO_SAMPLE_RING_CAP);
        g_imu.fifo_count--;
    }

    g_imu.fifo_ring[g_imu.fifo_widx].sample = *sample;
    g_imu.fifo_ring[g_imu.fifo_widx].ts_us = ts_us;
    g_imu.fifo_widx = (uint8_t)((g_imu.fifo_widx + 1U) % CONFIG_IMU_FIFO_SAMPLE_RING_CAP);
    g_imu.fifo_count++;
    imu_store_latest(sample, ts_us);
}

static bool imu_fifo_ring_pop(imu_sample_t *sample, uint64_t *ts_us)
{
    if ((sample == NULL) || (ts_us == NULL) || (g_imu.fifo_count == 0U)) {
        return false;
    }

    *sample = g_imu.fifo_ring[g_imu.fifo_ridx].sample;
    *ts_us = g_imu.fifo_ring[g_imu.fifo_ridx].ts_us;
    g_imu.fifo_ridx = (uint8_t)((g_imu.fifo_ridx + 1U) % CONFIG_IMU_FIFO_SAMPLE_RING_CAP);
    g_imu.fifo_count--;
    return true;
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

static int imu_fifo_flush(void)
{
    return imu_reg_write_cfg(&g_imu.cfg, REG_SIGNAL_PATH_RESET,
                             (uint8_t)ICM426XX_SIGNAL_PATH_RESET_FIFO_FLUSH_EN);
}

static int imu_fifo_configure(void)
{
    int rc;
    uint8_t cfg0 = 0U;
    uint8_t tmst_cfg = 0U;
    uint8_t fifo_cfg1 = 0U;

    rc = imu_reg_read_cfg(&g_imu.cfg, REG_INTF_CONFIG0, &cfg0);
    if (rc) {
        return rc;
    }

    cfg0 |= (uint8_t)ICM426XX_INTF_CONFIG0_FIFO_COUNT_REC_RECORD;
    cfg0 &= (uint8_t)~BIT_FIFO_COUNT_ENDIAN_MASK;
    g_imu.data_endian = (uint8_t)(cfg0 & BIT_DATA_ENDIAN_MASK);

    rc = imu_reg_write_cfg(&g_imu.cfg, REG_INTF_CONFIG0, cfg0);
    if (rc) {
        return rc;
    }

    rc = imu_reg_write_cfg(&g_imu.cfg, REG_FIFO_CONFIG,
                           (uint8_t)ICM426XX_FIFO_CONFIG_MODE_BYPASS);
    if (rc) {
        return rc;
    }

    rc = imu_reg_read_cfg(&g_imu.cfg, REG_TMST_CONFIG, &tmst_cfg);
    if (rc) {
        return rc;
    }
    tmst_cfg |= (uint8_t)ICM426XX_TMST_CONFIG_TMST_EN;
    rc = imu_reg_write_cfg(&g_imu.cfg, REG_TMST_CONFIG, tmst_cfg);
    if (rc) {
        return rc;
    }

    fifo_cfg1 = (uint8_t)(BIT_FIFO_CONFIG1_ACCEL_MASK |
                          BIT_FIFO_CONFIG1_GYRO_MASK |
                          BIT_FIFO_CONFIG1_TEMP_MASK |
                          BIT_FIFO_CONFIG1_TMST_FSYNC_MASK |
                          ICM426XX_FIFO_CONFIG1_WM_GT_TH_EN);
    rc = imu_reg_write_cfg(&g_imu.cfg, REG_FIFO_CONFIG1, fifo_cfg1);
    if (rc) {
        return rc;
    }

    rc = imu_reg_write_cfg(&g_imu.cfg, REG_FIFO_CONFIG2, 0x01U);
    if (rc) {
        return rc;
    }

    rc = imu_fifo_flush();
    if (rc) {
        return rc;
    }

    rc = imu_reg_write_cfg(&g_imu.cfg, REG_INT_SOURCE0,
                           (uint8_t)(BIT_INT_SOURCE0_FIFO_THS_INT1_EN |
                                     BIT_INT_SOURCE0_FIFO_FULL_INT1_EN));
    if (rc) {
        return rc;
    }

    rc = imu_reg_write_cfg(&g_imu.cfg, REG_FIFO_CONFIG,
                           (uint8_t)ICM426XX_FIFO_CONFIG_MODE_STREAM);
    if (rc) {
        return rc;
    }

    g_imu.fifo_enabled = true;
    g_imu.fifo_ridx = 0U;
    g_imu.fifo_widx = 0U;
    g_imu.fifo_count = 0U;
    return HAL_OK;
}

static bool imu_fifo_decode_packet(const uint8_t *packet, imu_sample_t *sample)
{
    const fifo_header_t *header = (const fifo_header_t *)packet;
    size_t idx = FIFO_HEADER_SIZE;

    if ((packet == NULL) || (sample == NULL)) {
        return false;
    }

    if (header->bits.msg_bit || !header->bits.accel_bit || !header->bits.gyro_bit ||
        header->bits.twentybits_bit) {
        return false;
    }

    sample->accel_x = (int16_t)imu_parse_u16(&packet[idx + 0]);
    sample->accel_y = (int16_t)imu_parse_u16(&packet[idx + 2]);
    sample->accel_z = (int16_t)imu_parse_u16(&packet[idx + 4]);
    idx += FIFO_ACCEL_DATA_SIZE;

    sample->gyro_x = (int16_t)imu_parse_u16(&packet[idx + 0]);
    sample->gyro_y = (int16_t)imu_parse_u16(&packet[idx + 2]);
    sample->gyro_z = (int16_t)imu_parse_u16(&packet[idx + 4]);
    idx += FIFO_GYRO_DATA_SIZE;

    sample->temp = (int16_t)(int8_t)packet[idx];
    return true;
}

static int imu_fifo_fetch_samples(void)
{
    int rc;
    uint8_t count_buf[2] = {0};
    uint8_t packet_buf[IMU_FIFO_PACKET_SIZE * IMU_FIFO_BATCH_PACKETS];
    uint16_t packet_count = 0U;

    rc = imu_reg_read_block_cfg(&g_imu.cfg, REG_FIFO_COUNTH, count_buf, sizeof(count_buf));
    if (rc) {
        return rc;
    }

    packet_count = (uint16_t)(((uint16_t)count_buf[1] << 8) | count_buf[0]);
    if (packet_count == 0U) {
        return HAL_EBUSY;
    }

    while (packet_count > 0U) {
        uint16_t batch_packets = MIN(packet_count, (uint16_t)IMU_FIFO_BATCH_PACKETS);
        size_t batch_bytes = (size_t)batch_packets * IMU_FIFO_PACKET_SIZE;
        uint64_t newest_ts_us;
        uint64_t base_ts_us;

        rc = imu_reg_read_block_cfg(&g_imu.cfg, REG_FIFO_DATA, packet_buf, batch_bytes);
        if (rc) {
            return rc;
        }

        newest_ts_us = imu_pop_irq_timestamps((uint8_t)batch_packets);
        if (newest_ts_us == 0U) {
            newest_ts_us = k_cyc_to_us_floor64(k_cycle_get_64());
        }
        base_ts_us = newest_ts_us - ((uint64_t)(batch_packets - 1U) * IMU_SAMPLE_PERIOD_US);

        for (uint16_t i = 0; i < batch_packets; i++) {
            imu_sample_t sample;
            if (!imu_fifo_decode_packet(&packet_buf[i * IMU_FIFO_PACKET_SIZE], &sample)) {
                continue;
            }
            imu_fifo_ring_push(&sample, base_ts_us + ((uint64_t)i * IMU_SAMPLE_PERIOD_US));
        }

        packet_count -= batch_packets;
    }

    return HAL_OK;
}

static int imu_direct_read_sample(imu_sample_t *sample, uint64_t *sample_ts_us)
{
    uint8_t raw[14] = {0};
    int rc;

    if ((sample == NULL) || (sample_ts_us == NULL)) {
        return HAL_EINVAL;
    }

    *sample_ts_us = imu_pop_irq_timestamps(1U);
    if (*sample_ts_us == 0U) {
        *sample_ts_us = k_cyc_to_us_floor64(k_cycle_get_64());
    }

    rc = imu_reg_read_block_cfg(&g_imu.cfg, REG_TEMP_DATA1, raw, sizeof(raw));
    if (rc) {
        return rc;
    }

    *sample = (imu_sample_t){
        .temp = (int16_t)((raw[0] << 8) | raw[1]),
        .accel_x = (int16_t)((raw[2] << 8) | raw[3]),
        .accel_y = (int16_t)((raw[4] << 8) | raw[5]),
        .accel_z = (int16_t)((raw[6] << 8) | raw[7]),
        .gyro_x = (int16_t)((raw[8] << 8) | raw[9]),
        .gyro_y = (int16_t)((raw[10] << 8) | raw[11]),
        .gyro_z = (int16_t)((raw[12] << 8) | raw[13]),
    };
    imu_store_latest(sample, *sample_ts_us);
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

    rc = imu_reg_write_cfg(&g_imu.cfg, REG_GYRO_CONFIG0, 0x08);
    if (rc) {
        LOG_ERR("imu gyro config0 failed: %d", rc);
        return rc;
    }

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

    if (IS_ENABLED(CONFIG_IMU_USE_FIFO)) {
        rc = imu_fifo_configure();
        if (rc == HAL_OK) {
            LOG_INF("imu fifo enabled (ring=%u)", CONFIG_IMU_FIFO_SAMPLE_RING_CAP);
            return HAL_OK;
        }
        LOG_WRN("imu fifo config failed: %d, falling back to direct path", rc);
        g_imu.fifo_enabled = false;
    }

    rc = imu_reg_write_cfg(&g_imu.cfg, REG_INT_SOURCE0, BIT_INT_SOURCE0_UI_DRDY_INT1_EN);
    if (rc) {
        LOG_ERR("imu int source0 failed: %d", rc);
        return rc;
    }

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
    LOG_INF("imu init done (LN, 100Hz, accel +/-16g, gyro +/-2000dps, fifo=%d)",
            g_imu.fifo_enabled ? 1 : 0);
    return HAL_OK;
}

static int imu_nrf_read_common(void *buf, size_t len, uint64_t *timestamp_us, int timeout_ms)
{
    imu_sample_t sample = {0};
    uint64_t sample_ts_us = 0U;

    if (!g_imu.inited) {
        return HAL_ENODEV;
    }
    if (buf == NULL || len < sizeof(imu_sample_t)) {
        return HAL_EINVAL;
    }

    if (g_imu.fifo_enabled && imu_fifo_ring_pop(&sample, &sample_ts_us)) {
        *(imu_sample_t *)buf = sample;
        if (timestamp_us != NULL) {
            *timestamp_us = sample_ts_us;
        }
        return HAL_OK;
    }

    k_timeout_t timeout = (timeout_ms < 0) ? K_FOREVER : K_MSEC(timeout_ms);
    int rc = k_sem_take(&g_imu_drdy_sem, timeout);
    if (rc) {
        return (rc == -EAGAIN) ? HAL_EBUSY : rc;
    }

    if (g_imu.fifo_enabled) {
        rc = imu_fifo_fetch_samples();
        if (rc != HAL_OK) {
            return rc;
        }
        if (!imu_fifo_ring_pop(&sample, &sample_ts_us)) {
            return HAL_EIO;
        }
    } else {
        rc = imu_direct_read_sample(&sample, &sample_ts_us);
        if (rc != HAL_OK) {
            return rc;
        }
    }

    *(imu_sample_t *)buf = sample;
    if (timestamp_us != NULL) {
        *timestamp_us = sample_ts_us;
    }
    return HAL_OK;
}

static int imu_nrf_read(void *buf, size_t len, int timeout_ms)
{
    return imu_nrf_read_common(buf, len, NULL, timeout_ms);
}

static int imu_nrf_read_timed(void *buf, size_t len, uint64_t *timestamp_us, int timeout_ms)
{
    return imu_nrf_read_common(buf, len, timestamp_us, timeout_ms);
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

static int imu_nrf_get_latest_us(imu_sample_t *out, uint64_t *timestamp_us)
{
    if ((out == NULL) || (timestamp_us == NULL)) {
        return HAL_EINVAL;
    }
    k_mutex_lock(&g_imu.latest_lock, K_FOREVER);
    if (!g_imu.latest_valid) {
        k_mutex_unlock(&g_imu.latest_lock);
        return HAL_ENODEV;
    }
    *out = g_imu.latest;
    *timestamp_us = g_imu.latest_ts_us;
    k_mutex_unlock(&g_imu.latest_lock);
    return HAL_OK;
}

static const hal_imu_ops_t g_imu_ops = {
    .init = imu_nrf_init,
    .read = imu_nrf_read,
    .read_timed = imu_nrf_read_timed,
    .get_latest = imu_nrf_get_latest,
    .get_latest_us = imu_nrf_get_latest_us,
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

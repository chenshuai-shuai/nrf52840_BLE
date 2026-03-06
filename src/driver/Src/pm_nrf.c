#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "hal_pm.h"
#include "error.h"

LOG_MODULE_REGISTER(pm_nrf, LOG_LEVEL_WRN);

#define PM_NODE DT_NODELABEL(max77658)

#if !DT_NODE_HAS_STATUS(PM_NODE, okay)
#error "max77658 devicetree node is not enabled"
#endif

/* Register addresses */
#define PM_REG_STAT_CHG_B   0x03
#define PM_REG_STAT_GLBL    0x06

#define PM_REG_CNFG_CHG_B   0x21
#define PM_REG_CNFG_CHG_D   0x23
#define PM_REG_CNFG_CHG_E   0x24
#define PM_REG_CNFG_CHG_G   0x26

#define PM_REG_CNFG_SBB0_A  0x39
#define PM_REG_CNFG_SBB0_B  0x3A
#define PM_REG_CNFG_SBB1_A  0x3B
#define PM_REG_CNFG_SBB1_B  0x3C
#define PM_REG_CNFG_SBB2_A  0x3D
#define PM_REG_CNFG_SBB2_B  0x3E
#define PM_REG_CNFG_GPIO1   0x12

/* CNFG_CHG_B fields */
#define CHG_B_VCHGIN_MIN_SHIFT 5
#define CHG_B_VCHGIN_MIN_MASK  (0x7u << CHG_B_VCHGIN_MIN_SHIFT)
#define CHG_B_ICHGIN_LIM_SHIFT 2
#define CHG_B_ICHGIN_LIM_MASK  (0x7u << CHG_B_ICHGIN_LIM_SHIFT)
#define CHG_B_I_PQ_MASK        (1u << 1)
#define CHG_B_CHG_EN_MASK      (1u << 0)

/* CNFG_CHG_D fields */
#define CHG_D_VSYS_REG_MASK 0x1Fu

/* CNFG_CHG_E fields */
#define CHG_E_CHG_CC_SHIFT 2
#define CHG_E_CHG_CC_MASK  (0x3Fu << CHG_E_CHG_CC_SHIFT)

/* CNFG_CHG_G fields */
#define CHG_G_CHG_CV_SHIFT 2
#define CHG_G_CHG_CV_MASK  (0x3Fu << CHG_G_CHG_CV_SHIFT)

/* CNFG_SBBx_B fields */
#define SBB_B_OP_MODE_SHIFT 6
#define SBB_B_OP_MODE_MASK  (0x3u << SBB_B_OP_MODE_SHIFT)
#define SBB_B_IP_SHIFT      4
#define SBB_B_IP_MASK       (0x3u << SBB_B_IP_SHIFT)
#define SBB_B_ADE_MASK      (1u << 3)
#define SBB_B_EN_MASK       0x7u

/* Default configuration targets */
#define PM_SBB_MV_DEFAULT     3300
#define PM_CHG_CV_MV_DEFAULT  4200
#define PM_CHG_CC_MA_DEFAULT  200

static const struct i2c_dt_spec g_i2c = I2C_DT_SPEC_GET(PM_NODE);
static const struct gpio_dt_spec g_nirq = GPIO_DT_SPEC_GET_OR(PM_NODE, nirq_gpios, {0});
static const struct gpio_dt_spec g_alrt = GPIO_DT_SPEC_GET_OR(PM_NODE, alrt_gpios, {0});

static int pm_reg_read(uint8_t reg, uint8_t *val)
{
    if (val == NULL) {
        return HAL_EINVAL;
    }
    int ret = i2c_reg_read_byte_dt(&g_i2c, reg, val);
    if (ret == 0) {
        LOG_DBG("pm rd 0x%02x -> 0x%02x", reg, *val);
        return HAL_OK;
    }
    LOG_ERR("pm rd 0x%02x failed: %d", reg, ret);
    return ret;
}

static int pm_reg_write(uint8_t reg, uint8_t val)
{
    int ret = i2c_reg_write_byte_dt(&g_i2c, reg, val);
    if (ret == 0) {
        LOG_DBG("pm wr 0x%02x <- 0x%02x", reg, val);
        return HAL_OK;
    }
    LOG_ERR("pm wr 0x%02x failed: %d", reg, ret);
    return ret;
}

static int pm_reg_update(uint8_t reg, uint8_t mask, uint8_t val)
{
    uint8_t cur = 0;
    int ret = pm_reg_read(reg, &cur);
    if (ret != HAL_OK) {
        return ret;
    }
    uint8_t next = (cur & ~mask) | (val & mask);
    return pm_reg_write(reg, next);
}

static uint8_t pm_encode_sbb_mv(uint16_t mv)
{
    if (mv <= 500) {
        return 0x00;
    }
    if (mv >= 5500) {
        return 0xC8;
    }
    /* 0.5V start, 25mV steps */
    uint16_t code = (uint16_t)((mv - 500) / 25);
    if (code > 0xC8) {
        code = 0xC8;
    }
    return (uint8_t)code;
}

static uint8_t pm_encode_chg_cv(uint16_t mv)
{
    if (mv <= 3600) {
        return 0x00;
    }
    if (mv >= 4600) {
        return 0x28;
    }
    /* 3.6V start, 25mV steps */
    uint16_t code = (uint16_t)((mv - 3600) / 25);
    if (code > 0x28) {
        code = 0x28;
    }
    return (uint8_t)code;
}

static uint8_t pm_encode_chg_cc(uint16_t ma)
{
    if (ma <= 8) {
        return 0x00;
    }
    if (ma >= 300) {
        return 0x27;
    }
    /* 7.5mA steps starting at 7.5mA */
    uint16_t steps = (uint16_t)((ma * 10u + 37u) / 75u);
    uint16_t code = steps;
    if (code == 0) {
        code = 1;
    }
    if (code > 0x27) {
        code = 0x27;
    }
    return (uint8_t)(code - 1);
}

static int pm_config_sbb(uint8_t reg_a, uint8_t reg_b,
                         uint16_t mv, uint8_t ip_code, bool enable)
{
    uint8_t vcode = pm_encode_sbb_mv(mv);
    LOG_INF("pm cfg sbb reg_a=0x%02x reg_b=0x%02x mv=%u code=0x%02x ip=0x%x en=%d",
            reg_a, reg_b, mv, vcode, ip_code, enable ? 1 : 0);
    int ret = pm_reg_write(reg_a, vcode);
    if (ret != HAL_OK) {
        return ret;
    }

    uint8_t en = enable ? 0x6u : 0x4u; /* force on/off */
    uint8_t val = ((0x0u << SBB_B_OP_MODE_SHIFT) & SBB_B_OP_MODE_MASK) |
                  ((ip_code << SBB_B_IP_SHIFT) & SBB_B_IP_MASK) |
                  (0u << 3) |
                  (en & SBB_B_EN_MASK);
    return pm_reg_update(reg_b, SBB_B_OP_MODE_MASK | SBB_B_IP_MASK | SBB_B_ADE_MASK | SBB_B_EN_MASK, val);
}

static int pm_config_charger(uint16_t cv_mv, uint16_t cc_ma, bool enable)
{
    LOG_INF("pm cfg chg cv=%umV cc=%umA en=%d", cv_mv, cc_ma, enable ? 1 : 0);
    /* CNFG_CHG_B: set input current limit to 285mA, CHG_EN */
    uint8_t chg_b = (0u << CHG_B_VCHGIN_MIN_SHIFT) |
                    (0x2u << CHG_B_ICHGIN_LIM_SHIFT) |
                    (0u << 1) |
                    (enable ? 1u : 0u);
    int ret = pm_reg_update(PM_REG_CNFG_CHG_B,
                            CHG_B_VCHGIN_MIN_MASK | CHG_B_ICHGIN_LIM_MASK | CHG_B_I_PQ_MASK | CHG_B_CHG_EN_MASK,
                            chg_b);
    if (ret != HAL_OK) {
        return ret;
    }

    /* CNFG_CHG_D: VSYS_REG >= VFAST_CHG + 200mV. Set 4.5V (0x16). */
    uint8_t vsys_code = 0x16u & CHG_D_VSYS_REG_MASK;
    ret = pm_reg_update(PM_REG_CNFG_CHG_D, CHG_D_VSYS_REG_MASK, vsys_code);
    if (ret != HAL_OK) {
        return ret;
    }

    /* CNFG_CHG_E: CHG_CC */
    uint8_t cc_code = pm_encode_chg_cc(cc_ma);
    uint8_t chg_e = (uint8_t)((cc_code << CHG_E_CHG_CC_SHIFT) & CHG_E_CHG_CC_MASK);
    ret = pm_reg_update(PM_REG_CNFG_CHG_E, CHG_E_CHG_CC_MASK, chg_e);
    if (ret != HAL_OK) {
        return ret;
    }

    /* CNFG_CHG_G: CHG_CV */
    uint8_t cv_code = pm_encode_chg_cv(cv_mv);
    uint8_t chg_g = (uint8_t)((cv_code << CHG_G_CHG_CV_SHIFT) & CHG_G_CHG_CV_MASK);
    return pm_reg_update(PM_REG_CNFG_CHG_G, CHG_G_CHG_CV_MASK, chg_g);
}

static int pm_nrf_init(void)
{
    if (!device_is_ready(g_i2c.bus)) {
        LOG_ERR("pm i2c bus not ready");
        return HAL_ENODEV;
    }

    LOG_INF("pm init: i2c addr=0x%02x", (unsigned)g_i2c.addr);
    if (g_nirq.port) {
        int ret = gpio_pin_configure_dt(&g_nirq, GPIO_INPUT);
        if (ret) {
            LOG_WRN("nirq gpio init failed: %d", ret);
        }
    }
    if (g_alrt.port) {
        int ret = gpio_pin_configure_dt(&g_alrt, GPIO_INPUT);
        if (ret) {
            LOG_WRN("alrt gpio init failed: %d", ret);
        }
    }

    return HAL_OK;
}

static int pm_nrf_set_mode(int mode)
{
    LOG_INF("pm set mode=%d", mode);
    if (mode == 0) {
        int ret = pm_config_sbb(PM_REG_CNFG_SBB0_A, PM_REG_CNFG_SBB0_B, PM_SBB_MV_DEFAULT, 0x2u, true);
        if (ret != HAL_OK) {
            return ret;
        }
        ret = pm_config_sbb(PM_REG_CNFG_SBB1_A, PM_REG_CNFG_SBB1_B, PM_SBB_MV_DEFAULT, 0x2u, true);
        if (ret != HAL_OK) {
            return ret;
        }
        ret = pm_config_sbb(PM_REG_CNFG_SBB2_A, PM_REG_CNFG_SBB2_B, PM_SBB_MV_DEFAULT, 0x0u, true);
        if (ret != HAL_OK) {
            return ret;
        }
        return pm_config_charger(PM_CHG_CV_MV_DEFAULT, PM_CHG_CC_MA_DEFAULT, true);
    }
    if (mode == 1) {
        int ret = pm_config_sbb(PM_REG_CNFG_SBB0_A, PM_REG_CNFG_SBB0_B, PM_SBB_MV_DEFAULT, 0x2u, false);
        if (ret != HAL_OK) {
            return ret;
        }
        ret = pm_config_sbb(PM_REG_CNFG_SBB1_A, PM_REG_CNFG_SBB1_B, PM_SBB_MV_DEFAULT, 0x2u, false);
        if (ret != HAL_OK) {
            return ret;
        }
        return pm_config_sbb(PM_REG_CNFG_SBB2_A, PM_REG_CNFG_SBB2_B, PM_SBB_MV_DEFAULT, 0x0u, false);
    }
    if (mode == 2) {
        return pm_config_charger(PM_CHG_CV_MV_DEFAULT, PM_CHG_CC_MA_DEFAULT, false);
    }
    if (mode == 3) {
        return pm_config_charger(PM_CHG_CV_MV_DEFAULT, PM_CHG_CC_MA_DEFAULT, true);
    }

    return HAL_ENOTSUP;
}

static int pm_nrf_get_status(int *status)
{
    if (status == NULL) {
        return HAL_EINVAL;
    }

    uint8_t stat_chg_b = 0;
    uint8_t stat_glbl = 0;
    int ret = pm_reg_read(PM_REG_STAT_CHG_B, &stat_chg_b);
    if (ret != HAL_OK) {
        return ret;
    }
    ret = pm_reg_read(PM_REG_STAT_GLBL, &stat_glbl);
    if (ret != HAL_OK) {
        return ret;
    }

    LOG_DBG("pm status raw glbl=0x%02x chg_b=0x%02x", stat_glbl, stat_chg_b);
    *status = ((int)stat_glbl << 8) | stat_chg_b;
    return HAL_OK;
}

static int pm_nrf_set_gpio1(int level)
{
    uint8_t mask = (1u << 5) | (1u << 4) | (1u << 3) | (1u << 2) | (1u << 0);
    uint8_t val = 0u;
    /* ALT_GPIO1=0 (GPIO), DBEN_GPI=0, DRV=0 (open-drain), DIR=0 (GPO) */
    if (level) {
        val |= (1u << 3); /* DO = 1 */
    }
    return pm_reg_update(PM_REG_CNFG_GPIO1, mask, val);
}

static const hal_pm_ops_t g_pm_ops = {
    .init = pm_nrf_init,
    .set_mode = pm_nrf_set_mode,
    .get_status = pm_nrf_get_status,
    .set_gpio1 = pm_nrf_set_gpio1,
};

int pm_nrf_register(void)
{
    return hal_pm_register(&g_pm_ops);
}

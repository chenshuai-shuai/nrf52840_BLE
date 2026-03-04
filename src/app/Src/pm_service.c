#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

#include "pm_service.h"
#include "system_state.h"
#include "hal_pm.h"
#include "error.h"
#include "rt_thread.h"

LOG_MODULE_REGISTER(pm_service, LOG_LEVEL_INF);

#define PM_SVC_STACK_SIZE 2048
#define PM_SVC_PRIORITY   7

#define FG_ADDR            0x36
#define FG_REG_STATUS      0x00
#define FG_REG_REPSOC      0x06
#define FG_REG_VCELL       0x09
#define FG_REG_CURRENT     0x0A
#define FG_REG_FULLSOCTHR  0x13
#define FG_REG_DESIGNCAP   0x18
#define FG_REG_ICHGTERM    0x1E
#define FG_REG_VEMPTY      0x3A
#define FG_REG_MODELCFG    0xDB

/* Default fuel-gauge config for generic 3.7V 550mAh Li-ion */
#define FG_DESIGNCAP_MAH   550U
#define FG_FULLSOCTHR_EZ   0x5005U /* 80% recommended for EZ performance */
#define FG_ICHGTERM_MA     20U     /* assume 20mA termination current */
#define FG_RSENSE_MOHM     47U     /* estimated Rsense (mOhm) */
#define FG_VEMPTY_MV       3300U   /* empty voltage */
#define FG_VRECOV_MV       3600U   /* recovery voltage */

static struct k_thread g_pm_svc_thread;
RT_THREAD_STACK_DEFINE(g_pm_svc_stack, PM_SVC_STACK_SIZE);
static bool g_pm_svc_started;
static volatile bool g_pm_svc_ready;
static volatile int g_pm_svc_last_error;

static int fg_read16(const struct device *i2c, uint8_t reg, uint16_t *val)
{
    uint8_t buf[2] = {0};
    if (i2c_write_read(i2c, FG_ADDR, &reg, 1, buf, 2) != 0) {
        return -EIO;
    }
    *val = ((uint16_t)buf[1] << 8) | buf[0];
    return 0;
}

static int fg_write16(const struct device *i2c, uint8_t reg, uint16_t val)
{
    uint8_t buf[3];
    buf[0] = reg;
    buf[1] = (uint8_t)(val & 0xFF);
    buf[2] = (uint8_t)(val >> 8);
    return i2c_write(i2c, buf, sizeof(buf), FG_ADDR);
}

static uint16_t fg_encode_vempty(uint16_t vempty_mv, uint16_t vrecov_mv)
{
    /* VE: 10mV/LSB (9 bits), VR: 40mV/LSB (7 bits) */
    uint16_t ve = (uint16_t)(vempty_mv / 10U);
    uint16_t vr = (uint16_t)(vrecov_mv / 40U);
    if (ve > 0x1FF) {
        ve = 0x1FF;
    }
    if (vr > 0x7F) {
        vr = 0x7F;
    }
    return (uint16_t)((ve << 7) | (vr & 0x7F));
}

static void fg_init_default(const struct device *i2c)
{
    uint16_t status = 0;
    if (fg_read16(i2c, FG_REG_STATUS, &status) != 0) {
        LOG_WRN("pm svc: fg status read failed");
        return;
    }

    if ((status & 0x0002u) == 0) {
        /* Not a POR reset; keep learned model */
        LOG_INF("pm svc: fg POR not set, skip model refresh");
        return;
    }

    uint16_t designcap = (uint16_t)(FG_DESIGNCAP_MAH * 10U); /* 0.1mAh LSB */
    uint16_t vempty = fg_encode_vempty(FG_VEMPTY_MV, FG_VRECOV_MV);

    (void)fg_write16(i2c, FG_REG_FULLSOCTHR, FG_FULLSOCTHR_EZ);
    (void)fg_write16(i2c, FG_REG_DESIGNCAP, designcap);

    /* IChgTerm uses current register units (LSB 33.487uA) */
    uint16_t ichgterm = (uint16_t)((FG_ICHGTERM_MA * 1000U + 16U) / 33U);
    (void)fg_write16(i2c, FG_REG_ICHGTERM, ichgterm);
    (void)fg_write16(i2c, FG_REG_VEMPTY, vempty);

    /* Trigger model refresh */
    if (fg_write16(i2c, FG_REG_MODELCFG, 0x8000u) == 0) {
        uint16_t cfg = 0;
        for (int i = 0; i < 50; i++) {
            if (fg_read16(i2c, FG_REG_MODELCFG, &cfg) == 0 && (cfg & 0x8000u) == 0) {
                break;
            }
            k_msleep(10);
        }
    }

    /* Clear POR bit */
    (void)fg_write16(i2c, FG_REG_STATUS, (uint16_t)(status & ~0x0002u));
}

static void pm_service_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    const struct device *i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));
    if (!device_is_ready(i2c)) {
        LOG_ERR("pm svc: i2c0 not ready");
        g_pm_svc_last_error = HAL_ENODEV;
        g_pm_svc_ready = false;
        return;
    }

    /* Wait for PMIC to be ready before configuring rails */
    int ret = hal_pm_init();
    while (ret != HAL_OK) {
        LOG_WRN("pm svc: pm init retry: %d", ret);
        g_pm_svc_last_error = ret;
        k_msleep(200);
        ret = hal_pm_init();
    }

    k_msleep(200);
    int status = 0;
    for (int i = 0; i < 20; i++) {
        if (hal_pm_get_status(&status) == HAL_OK) {
            uint8_t stat_chg_b = (uint8_t)(status & 0xFF);
            uint8_t chgin_dtls = (stat_chg_b >> 2) & 0x03;
            if (chgin_dtls == 0x03) {
                break;
            }
        }
        k_msleep(50);
    }

    ret = hal_pm_set_mode(HAL_PM_MODE_DEFAULT);
    if (ret != HAL_OK) {
        LOG_ERR("pm svc: pm default config failed: %d", ret);
        g_pm_svc_last_error = ret;
        g_pm_svc_ready = false;
    } else {
        g_pm_svc_last_error = HAL_OK;
        g_pm_svc_ready = true;
    }

    /* Fuel-gauge init (POR only) */
    fg_init_default(i2c);

    while (1) {
        pm_state_t next = {0};
        int status = 0;
        if (hal_pm_get_status(&status) == HAL_OK) {
            uint8_t stat_chg_b = (uint8_t)(status & 0xFF);
            next.chg_dtls = (stat_chg_b >> 4) & 0x0F;
            next.chgin_dtls = (stat_chg_b >> 2) & 0x03;
            next.chg = (stat_chg_b >> 1) & 0x01;
            next.time_sus = stat_chg_b & 0x01;
        }

        uint16_t raw = 0;
        if (fg_read16(i2c, FG_REG_REPSOC, &raw) == 0) {
            uint16_t soc_int = (uint16_t)(raw >> 8);
            uint16_t soc_frac = (uint16_t)(((uint32_t)(raw & 0xFF) * 100U) / 256U);
            next.soc_x100 = (uint16_t)(soc_int * 100U + soc_frac);
        }

        if (fg_read16(i2c, FG_REG_VCELL, &raw) == 0) {
            next.vcell_mv = (uint16_t)(((uint32_t)raw * 78125U) / 1000000U);
        }

        if (fg_read16(i2c, FG_REG_CURRENT, &raw) == 0) {
            int16_t cur_raw = (int16_t)raw;
            int32_t ma = (int32_t)cur_raw * 15625 / (int32_t)FG_RSENSE_MOHM / 10000;
            next.current_ma = (int16_t)ma;
        }

        next.timestamp_ms = (uint32_t)k_uptime_get();
        next.valid = 1;
        system_state_set_pm(&next);

        if (IS_ENABLED(CONFIG_PM_SERVICE_DEBUG_LOG)) {
            LOG_INF("pm svc: soc=%u.%02u%% v=%umV i=%dmA chg=%u",
                    next.soc_x100 / 100, next.soc_x100 % 100,
                    next.vcell_mv, next.current_ma, next.chg);
        }

        k_msleep(CONFIG_PM_SERVICE_INTERVAL_MS);
    }
}

int pm_service_start(void)
{
    if (g_pm_svc_started) {
        return HAL_OK;
    }

    int ret = rt_thread_start(&g_pm_svc_thread,
                              g_pm_svc_stack,
                              K_THREAD_STACK_SIZEOF(g_pm_svc_stack),
                              pm_service_entry,
                              NULL, NULL, NULL,
                              PM_SVC_PRIORITY, 0,
                              "pm_service");
    if (ret != 0) {
        LOG_ERR("pm svc: thread start failed: %d", ret);
        g_pm_svc_last_error = HAL_EIO;
        g_pm_svc_ready = false;
        return HAL_EIO;
    }

    g_pm_svc_started = true;
    g_pm_svc_ready = false;
    g_pm_svc_last_error = 0;
    return HAL_OK;
}

bool pm_service_is_ready(void)
{
    return g_pm_svc_ready;
}

int pm_service_last_error(void)
{
    return (int)g_pm_svc_last_error;
}

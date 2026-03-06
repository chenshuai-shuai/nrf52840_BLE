#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

#include "app_pm_test.h"
#include "hal_pm.h"
#include "error.h"
#include "rt_thread.h"
#include "system_state.h"

LOG_MODULE_REGISTER(app_pm_test, LOG_LEVEL_WRN);

#if IS_ENABLED(CONFIG_PM_SERVICE_DEBUG_LOG)
#define PM_DBG_INF(...) LOG_INF(__VA_ARGS__)
#else
#define PM_DBG_INF(...)
#endif

#define PM_TEST_STACK_SIZE 2048
#define PM_TEST_PRIORITY   5

#define FG_ADDR            0x36
#define FG_REG_STATUS      0x00
#define FG_REG_REPCAP      0x05
#define FG_REG_REPSOC      0x06
#define FG_REG_VCELL       0x09
#define FG_REG_CURRENT     0x0A
#define FG_REG_FULLSOCTHR  0x13
#define FG_REG_DESIGNCAP   0x18
#define FG_REG_CONFIG      0x1D
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

static struct k_thread g_pm_thread;
RT_THREAD_STACK_DEFINE(g_pm_stack, PM_TEST_STACK_SIZE);

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
        LOG_WRN("fg init: status read failed");
        return;
    }

    if ((status & 0x0002u) == 0) {
        /* Not a POR reset; keep learned model */
        LOG_INF("fg init: POR not set, skip model refresh");
        return;
    }

    /* Program basic parameters for generic cell */
    uint16_t designcap = (uint16_t)(FG_DESIGNCAP_MAH * 10U); /* 0.1mAh LSB */
    uint16_t vempty = fg_encode_vempty(FG_VEMPTY_MV, FG_VRECOV_MV);

    if (fg_write16(i2c, FG_REG_FULLSOCTHR, FG_FULLSOCTHR_EZ) == 0) {
        LOG_INF("fg init: FullSOCThr=0x%04x", FG_FULLSOCTHR_EZ);
    } else {
        LOG_WRN("fg init: FullSOCThr write failed");
    }

    if (fg_write16(i2c, FG_REG_DESIGNCAP, designcap) == 0) {
        LOG_INF("fg init: DesignCap=%u mAh (0x%04x)", FG_DESIGNCAP_MAH, designcap);
    } else {
        LOG_WRN("fg init: DesignCap write failed");
    }

    /* IChgTerm uses current register units (LSB 33.487uA) */
    uint16_t ichgterm = (uint16_t)((FG_ICHGTERM_MA * 1000U + 16U) / 33U);
    if (fg_write16(i2c, FG_REG_ICHGTERM, ichgterm) == 0) {
        LOG_INF("fg init: IChgTerm=%u mA (0x%04x)", FG_ICHGTERM_MA, ichgterm);
    } else {
        LOG_WRN("fg init: IChgTerm write failed");
    }

    if (fg_write16(i2c, FG_REG_VEMPTY, vempty) == 0) {
        LOG_INF("fg init: VEmpty=0x%04x (VE=%umV VR=%umV)", vempty, FG_VEMPTY_MV, FG_VRECOV_MV);
    } else {
        LOG_WRN("fg init: VEmpty write failed");
    }

    /* Trigger model refresh */
    if (fg_write16(i2c, FG_REG_MODELCFG, 0x8000u) != 0) {
        LOG_WRN("fg init: ModelCfg write failed");
    } else {
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

static void pm_test_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("pm test: start");

    const struct device *i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));
    if (!device_is_ready(i2c)) {
        LOG_ERR("i2c0 not ready");
        return;
    }

    k_msleep(200);
    uint8_t found = 0x00;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        uint8_t reg = 0x00;
        uint8_t val = 0x00;
        int r = i2c_write_read(i2c, addr, &reg, 1, &val, 1);
        if (r == 0) {
            LOG_INF("pm test: i2c ack at 0x%02x (reg0=0x%02x)", addr, val);
            if (addr == 0x40 || addr == 0x48) {
                found = addr;
                break;
            }
        }
    }

    if (found == 0x00) {
        LOG_ERR("pmic i2c address not found (scan 0x08-0x77)");
        return;
    }

    uint16_t dt_addr = (uint16_t)DT_REG_ADDR(DT_NODELABEL(max77658));
    if (dt_addr != found) {
        LOG_WRN("pmic dt addr=0x%02x mismatch, update overlay to match", dt_addr);
    }

    int ret = hal_pm_init();
    if (ret != HAL_OK) {
        LOG_ERR("pm init failed: %d", ret);
        return;
    }

    ret = hal_pm_set_mode(HAL_PM_MODE_DEFAULT);
    if (ret != HAL_OK) {
        LOG_ERR("pm default config failed: %d", ret);
        return;
    }

    LOG_INF("pm test: config done");

    /* Fuel-gauge basic init for generic cell */
    fg_init_default(i2c);

    uint64_t last_toggle = 0;
    bool led_on = false;

    while (1) {
        int status = 0;
        uint8_t chg_dtls = 0;
        uint8_t chgin_dtls = 0;
        uint8_t chg = 0;
        uint8_t time_sus = 0;
        ret = hal_pm_get_status(&status);
        if (ret == HAL_OK) {
            uint8_t stat_chg_b = (uint8_t)(status & 0xFF);
            uint8_t stat_glbl = (uint8_t)((status >> 8) & 0xFF);
            chg_dtls = (stat_chg_b >> 4) & 0x0F;
            chgin_dtls = (stat_chg_b >> 2) & 0x03;
            chg = (stat_chg_b >> 1) & 0x01;
            time_sus = stat_chg_b & 0x01;

            PM_DBG_INF("pm stat: glbl=0x%02x chg_b=0x%02x chg_dtls=%u chgin_dtls=%u chg=%u time_sus=%u",
                       stat_glbl, stat_chg_b, chg_dtls, chgin_dtls, chg, time_sus);
            ARG_UNUSED(stat_glbl);

            /* LED policy on GPIO1:
             * - charging (chg=1): blink 1Hz
             * - done (chg_dtls=8/9): steady on
             * - else: off
             * GPIO1 LED is active-low (LED to 3.3V_AON).
             */
            uint64_t now = k_uptime_get();
            if (chg) {
                if (now - last_toggle >= 500) {
                    led_on = !led_on;
                    last_toggle = now;
                }
            } else if (chg_dtls == 8 || chg_dtls == 9) {
                led_on = true;
            } else {
                led_on = false;
            }

            /* active-low LED: drive low to turn on */
            (void)hal_pm_set_gpio1(led_on ? 0 : 1);
        } else {
            LOG_ERR("pm status read failed: %d", ret);
        }

        /* Fuel gauge (I2C 0x36) */
        {
            uint16_t raw = 0;
            pm_state_t state = {0};
            if (fg_read16(i2c, FG_REG_REPSOC, &raw) == 0) {
                uint16_t soc_int = (uint16_t)(raw >> 8);
                uint16_t soc_frac = (uint16_t)(((uint32_t)(raw & 0xFF) * 100U) / 256U);
                PM_DBG_INF("fg soc: %u.%02u %% (raw=0x%04x)", soc_int, soc_frac, raw);

                /* Estimate capacity from SOC and design capacity */
                uint32_t soc_x100 = (uint32_t)soc_int * 100U + soc_frac;
                uint32_t cap_est = (FG_DESIGNCAP_MAH * soc_x100) / 10000U;
                PM_DBG_INF("fg cap est: %u mAh (design=%u mAh)", cap_est, FG_DESIGNCAP_MAH);
                ARG_UNUSED(cap_est);

                state.soc_x100 = (uint16_t)soc_x100;
            } else {
                LOG_WRN("fg soc read failed");
            }

            if (fg_read16(i2c, FG_REG_VCELL, &raw) == 0) {
                /* Use fixed-point: mV = raw * 78.125uV => raw * 78125 / 1,000,000 */
                uint32_t mv = (uint32_t)raw * 78125U / 1000000U;
                PM_DBG_INF("fg vcell: %u mV (raw=0x%04x)", mv, raw);
                state.vcell_mv = (uint16_t)mv;
            } else {
                LOG_WRN("fg vcell read failed");
            }

            if (fg_read16(i2c, FG_REG_CURRENT, &raw) == 0) {
                int16_t cur_raw = (int16_t)raw;
                /* Estimate: LSB = 1.5625uV / Rsense => mA = raw * 1.5625 / Rsense(mOhm) */
                int32_t ma = (int32_t)cur_raw * 15625 / (int32_t)FG_RSENSE_MOHM / 10000;
                PM_DBG_INF("fg current est: %d mA (raw=0x%04x, rsense=%u mOhm)",
                           (int)ma, (uint16_t)cur_raw, FG_RSENSE_MOHM);
                state.current_ma = (int16_t)ma;
            } else {
                LOG_WRN("fg current read failed");
            }

            if (fg_read16(i2c, FG_REG_REPCAP, &raw) == 0) {
                /* Raw RepCap for reference (scaling depends on Rsense) */
                PM_DBG_INF("fg repcap raw: 0x%04x", raw);
            } else {
                LOG_WRN("fg repcap read failed");
            }

            state.chg_dtls = chg_dtls;
            state.chgin_dtls = chgin_dtls;
            state.chg = chg;
            state.time_sus = time_sus;
            state.timestamp_ms = (uint32_t)k_uptime_get();
            state.valid = 1;
            system_state_set_pm(&state);
        }
        k_msleep(1000);
    }
}

void app_pm_test_start(void)
{
    static bool started;
    if (started) {
        return;
    }

    int ret = rt_thread_start(&g_pm_thread,
                              g_pm_stack,
                              K_THREAD_STACK_SIZEOF(g_pm_stack),
                              pm_test_entry,
                              NULL, NULL, NULL,
                              PM_TEST_PRIORITY, 0,
                              "pm_test");
    if (ret != 0) {
        LOG_ERR("pm test: thread start failed: %d", ret);
        return;
    }

    started = true;
}

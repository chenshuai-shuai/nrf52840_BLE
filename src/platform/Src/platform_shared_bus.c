#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <hal/nrf_i2s.h>
#include <hal/nrf_pdm.h>

#include "error.h"
#include "platform_shared_bus.h"

LOG_MODULE_REGISTER(platform_shared_bus, LOG_LEVEL_INF);

#define AUDIO_GPIO0_NODE DT_NODELABEL(gpio0)
#define AUDIO_GPIO1_NODE DT_NODELABEL(gpio1)
#define PDM_NODE         DT_NODELABEL(pdm0)
#define I2S_NODE         DT_NODELABEL(i2s0)
#define AMP_SD_MODE_NODE DT_ALIAS(amp_sd_mode)
#define WIFI_BOOT_CTRL_NODE DT_ALIAS(wifi_boot_ctrl)

#if !DT_NODE_HAS_STATUS(PDM_NODE, okay)
#error "pdm0 must be enabled for shared bus management"
#endif

#if !DT_NODE_HAS_STATUS(I2S_NODE, okay)
#error "i2s0 must be enabled for shared bus management"
#endif

#if !DT_NODE_HAS_STATUS(AMP_SD_MODE_NODE, okay)
#error "amp-sd-mode alias is missing"
#endif

#if !DT_NODE_HAS_STATUS(WIFI_BOOT_CTRL_NODE, okay)
#error "wifi-boot-ctrl alias is missing"
#endif

PINCTRL_DT_DEFINE(PDM_NODE);
PINCTRL_DT_DEFINE(I2S_NODE);

static const struct pinctrl_dev_config *const g_pdm_pcfg =
    PINCTRL_DT_DEV_CONFIG_GET(PDM_NODE);
static const struct pinctrl_dev_config *const g_i2s_pcfg =
    PINCTRL_DT_DEV_CONFIG_GET(I2S_NODE);

static const struct gpio_dt_spec g_amp_sd_mode =
    GPIO_DT_SPEC_GET(AMP_SD_MODE_NODE, gpios);
static const struct gpio_dt_spec g_boot_ctrl =
    GPIO_DT_SPEC_GET(WIFI_BOOT_CTRL_NODE, gpios);

struct shared_pin_desc {
    const struct device *port;
    gpio_pin_t pin;
    const char *name;
};

static const struct shared_pin_desc g_audio_shared_pins[] = {
    { DEVICE_DT_GET(AUDIO_GPIO0_NODE), 26U, "MIC_CLK"  },
    { DEVICE_DT_GET(AUDIO_GPIO1_NODE), 0U,  "MIC_DOUT" },
    { DEVICE_DT_GET(AUDIO_GPIO1_NODE), 11U, "SD_MODE"  },
    { DEVICE_DT_GET(AUDIO_GPIO1_NODE), 6U,  "AMP_DIN"  },
    { DEVICE_DT_GET(AUDIO_GPIO1_NODE), 2U,  "AMP_BCLK" },
    { DEVICE_DT_GET(AUDIO_GPIO1_NODE), 4U,  "AMP_LRCLK"},
};

static struct {
    bool inited;
    platform_shared_bus_mode_t mode;
    struct k_mutex lock;
} g_shared_bus = {
    .mode = PLATFORM_SHARED_BUS_SAFE_HANDOFF,
};

static bool shared_bus_pdm_clk_disconnected(uint32_t raw)
{
    return ((raw & PDM_PSEL_CLK_CONNECT_Msk) >>
            PDM_PSEL_CLK_CONNECT_Pos) == PDM_PSEL_CLK_CONNECT_Disconnected;
}

static bool shared_bus_pdm_din_disconnected(uint32_t raw)
{
    return ((raw & PDM_PSEL_DIN_CONNECT_Msk) >>
            PDM_PSEL_DIN_CONNECT_Pos) == PDM_PSEL_DIN_CONNECT_Disconnected;
}

static bool shared_bus_i2s_pin_disconnected(uint32_t raw, uint32_t connect_mask, uint32_t connect_pos)
{
    if (raw == NRF_I2S_PIN_NOT_CONNECTED) {
        return true;
    }

    return ((raw & connect_mask) >> connect_pos) == I2S_PSEL_SCK_CONNECT_Disconnected;
}

static void shared_bus_log_release_check_locked(const char *tag)
{
    uint32_t pdm_clk = nrf_pdm_clk_pin_get(NRF_PDM0);
    uint32_t pdm_din = nrf_pdm_din_pin_get(NRF_PDM0);
    uint32_t i2s_sck = nrf_i2s_sck_pin_get(NRF_I2S0);
    uint32_t i2s_lrck = nrf_i2s_lrck_pin_get(NRF_I2S0);
    uint32_t i2s_sdout = nrf_i2s_sdout_pin_get(NRF_I2S0);

    LOG_INF("shared bus chk[%s]: PDM CLK=0x%08x disc=%d DIN=0x%08x disc=%d",
            tag,
            (unsigned int)pdm_clk,
            shared_bus_pdm_clk_disconnected(pdm_clk) ? 1 : 0,
            (unsigned int)pdm_din,
            shared_bus_pdm_din_disconnected(pdm_din) ? 1 : 0);
    LOG_INF("shared bus chk[%s]: I2S SCK=0x%08x disc=%d LRCK=0x%08x disc=%d SDOUT=0x%08x disc=%d",
            tag,
            (unsigned int)i2s_sck,
            shared_bus_i2s_pin_disconnected(i2s_sck,
                                            I2S_PSEL_SCK_CONNECT_Msk,
                                            I2S_PSEL_SCK_CONNECT_Pos) ? 1 : 0,
            (unsigned int)i2s_lrck,
            shared_bus_i2s_pin_disconnected(i2s_lrck,
                                            I2S_PSEL_LRCK_CONNECT_Msk,
                                            I2S_PSEL_LRCK_CONNECT_Pos) ? 1 : 0,
            (unsigned int)i2s_sdout,
            shared_bus_i2s_pin_disconnected(i2s_sdout,
                                            I2S_PSEL_SDOUT_CONNECT_Msk,
                                            I2S_PSEL_SDOUT_CONNECT_Pos) ? 1 : 0);
    LOG_INF("shared bus chk[%s]: SD_MODE is GPIO-only (no peripheral PSEL owner)", tag);
}

static int shared_bus_ensure_gpio_ready(const struct gpio_dt_spec *spec, const char *name)
{
    if (!gpio_is_ready_dt(spec)) {
        LOG_ERR("shared bus: gpio not ready for %s", name);
        return HAL_ENODEV;
    }

    return HAL_OK;
}

static int shared_bus_release_peripheral_pins_locked(void)
{
    nrf_pdm_psel_disconnect(NRF_PDM0);

    nrf_i2s_pins_t i2s_pins = {
        .sck_pin = NRF_I2S_PIN_NOT_CONNECTED,
        .lrck_pin = NRF_I2S_PIN_NOT_CONNECTED,
        .mck_pin = NRF_I2S_PIN_NOT_CONNECTED,
        .sdout_pin = NRF_I2S_PIN_NOT_CONNECTED,
        .sdin_pin = NRF_I2S_PIN_NOT_CONNECTED,
    };
    nrf_i2s_pins_set(NRF_I2S0, &i2s_pins);

#if !IS_ENABLED(CONFIG_PM_DEVICE)
    return HAL_OK;
#else
    int ret = pinctrl_apply_state(g_pdm_pcfg, PINCTRL_STATE_SLEEP);
    if (ret != 0 && ret != -ENOENT) {
        LOG_ERR("shared bus: pdm0 sleep pinctrl failed: %d", ret);
        return ret;
    }
    if (ret == -ENOENT) {
        LOG_WRN("shared bus: pdm0 sleep pinctrl unavailable, falling back to gpio hi-z");
    }

    ret = pinctrl_apply_state(g_i2s_pcfg, PINCTRL_STATE_SLEEP);
    if (ret != 0 && ret != -ENOENT) {
        LOG_ERR("shared bus: i2s0 sleep pinctrl failed: %d", ret);
        return ret;
    }
    if (ret == -ENOENT) {
        LOG_WRN("shared bus: i2s0 sleep pinctrl unavailable, falling back to gpio hi-z");
    }

    return HAL_OK;
#endif
}

static int shared_bus_force_audio_pins_hiz_locked(void)
{
    int ret = shared_bus_release_peripheral_pins_locked();
    if (ret != HAL_OK) {
        return ret;
    }

    for (size_t i = 0; i < ARRAY_SIZE(g_audio_shared_pins); i++) {
        const struct shared_pin_desc *pin = &g_audio_shared_pins[i];

        if (!device_is_ready(pin->port)) {
            LOG_ERR("shared bus: gpio not ready for %s", pin->name);
            return HAL_ENODEV;
        }

        ret = gpio_pin_configure(pin->port, pin->pin, GPIO_INPUT);
        if (ret != 0) {
            LOG_ERR("shared bus: %s -> hi-z failed: %d", pin->name, ret);
            return ret;
        }
    }

    return HAL_OK;
}

static int shared_bus_apply_audio_mode_locked(void)
{
    int ret = shared_bus_force_audio_pins_hiz_locked();
    if (ret != HAL_OK) {
        return ret;
    }

    ret = shared_bus_ensure_gpio_ready(&g_amp_sd_mode, "amp-sd-mode");
    if (ret != HAL_OK) {
        return ret;
    }

    ret = pinctrl_apply_state(g_pdm_pcfg, PINCTRL_STATE_DEFAULT);
    if (ret != 0) {
        LOG_ERR("shared bus: pdm0 default pinctrl failed: %d", ret);
        return ret;
    }

    ret = pinctrl_apply_state(g_i2s_pcfg, PINCTRL_STATE_DEFAULT);
    if (ret != 0) {
        LOG_ERR("shared bus: i2s0 default pinctrl failed: %d", ret);
        return ret;
    }

    ret = gpio_pin_configure_dt(&g_amp_sd_mode, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
        LOG_ERR("shared bus: amp-sd-mode audio cfg failed: %d", ret);
        return ret;
    }

    g_shared_bus.mode = PLATFORM_SHARED_BUS_NRF_AUDIO;
    return HAL_OK;
}

static int shared_bus_apply_bootctrl_mode_locked(void)
{
    int ret = shared_bus_force_audio_pins_hiz_locked();
    if (ret != HAL_OK) {
        return ret;
    }

    ret = shared_bus_ensure_gpio_ready(&g_boot_ctrl, "wifi-boot-ctrl");
    if (ret != HAL_OK) {
        return ret;
    }

    ret = gpio_pin_configure_dt(&g_boot_ctrl, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
        LOG_ERR("shared bus: boot-ctrl cfg failed: %d", ret);
        return ret;
    }

    LOG_INF("shared bus: bootctrl gpio ready boot=P%d.%02d active_%s",
            g_boot_ctrl.port == DEVICE_DT_GET(DT_NODELABEL(gpio1)) ? 1 : 0,
            (int)g_boot_ctrl.pin,
            (g_boot_ctrl.dt_flags & GPIO_ACTIVE_LOW) ? "low" : "high");

    g_shared_bus.mode = PLATFORM_SHARED_BUS_NRF_BOOTCTRL;
    return HAL_OK;
}

int platform_shared_bus_init(void)
{
    if (g_shared_bus.inited) {
        return HAL_OK;
    }

    k_mutex_init(&g_shared_bus.lock);
    g_shared_bus.inited = true;

    k_mutex_lock(&g_shared_bus.lock, K_FOREVER);
    int ret = shared_bus_force_audio_pins_hiz_locked();
    if (ret == HAL_OK) {
        g_shared_bus.mode = PLATFORM_SHARED_BUS_SAFE_HANDOFF;
        LOG_INF("shared bus: initialized in SAFE_HANDOFF");
        shared_bus_log_release_check_locked("init_safe");
    }
    k_mutex_unlock(&g_shared_bus.lock);

    return ret;
}

platform_shared_bus_mode_t platform_shared_bus_get_mode(void)
{
    return g_shared_bus.mode;
}

int platform_shared_bus_enter_safe_handoff(void)
{
    int ret;

    ret = platform_shared_bus_init();
    if (ret != HAL_OK) {
        return ret;
    }

    k_mutex_lock(&g_shared_bus.lock, K_FOREVER);
    ret = shared_bus_force_audio_pins_hiz_locked();
    if (ret == HAL_OK) {
        g_shared_bus.mode = PLATFORM_SHARED_BUS_SAFE_HANDOFF;
        shared_bus_log_release_check_locked("safe_handoff");
    }
    k_mutex_unlock(&g_shared_bus.lock);

    if (ret == HAL_OK) {
        LOG_INF("shared bus: mode -> SAFE_HANDOFF");
    }
    return ret;
}

int platform_shared_bus_enter_nrf_audio(void)
{
    int ret;

    ret = platform_shared_bus_init();
    if (ret != HAL_OK) {
        return ret;
    }

    k_mutex_lock(&g_shared_bus.lock, K_FOREVER);
    ret = shared_bus_apply_audio_mode_locked();
    k_mutex_unlock(&g_shared_bus.lock);

    if (ret == HAL_OK) {
        LOG_INF("shared bus: mode -> NRF_AUDIO");
    }
    return ret;
}

int platform_shared_bus_enter_nrf_bootctrl(void)
{
    int ret;

    ret = platform_shared_bus_init();
    if (ret != HAL_OK) {
        return ret;
    }

    k_mutex_lock(&g_shared_bus.lock, K_FOREVER);
    ret = shared_bus_apply_bootctrl_mode_locked();
    k_mutex_unlock(&g_shared_bus.lock);

    if (ret == HAL_OK) {
        LOG_INF("shared bus: mode -> NRF_BOOTCTRL");
    }
    return ret;
}

int platform_shared_bus_set_amp_sd(bool enable)
{
    int ret;

    ret = platform_shared_bus_init();
    if (ret != HAL_OK) {
        return ret;
    }

    k_mutex_lock(&g_shared_bus.lock, K_FOREVER);
    if (g_shared_bus.mode != PLATFORM_SHARED_BUS_NRF_AUDIO) {
        if (!enable) {
            k_mutex_unlock(&g_shared_bus.lock);
            return HAL_OK;
        }
        k_mutex_unlock(&g_shared_bus.lock);
        return HAL_EBUSY;
    }

    ret = gpio_pin_set_dt(&g_amp_sd_mode, enable ? 1 : 0);
    k_mutex_unlock(&g_shared_bus.lock);

    return (ret == 0) ? HAL_OK : ret;
}

int platform_shared_bus_set_boot_signal(bool boot_asserted)
{
    int ret;

    ret = platform_shared_bus_init();
    if (ret != HAL_OK) {
        return ret;
    }

    k_mutex_lock(&g_shared_bus.lock, K_FOREVER);
    if (g_shared_bus.mode != PLATFORM_SHARED_BUS_NRF_BOOTCTRL) {
        k_mutex_unlock(&g_shared_bus.lock);
        return HAL_EBUSY;
    }

    LOG_INF("shared bus: boot signal request boot_asserted=%d -> ctrl boot=%d",
            boot_asserted ? 1 : 0,
            boot_asserted ? 1 : 0);

    ret = gpio_pin_set_dt(&g_boot_ctrl, boot_asserted ? 1 : 0);
    k_mutex_unlock(&g_shared_bus.lock);

    return (ret == 0) ? HAL_OK : ret;
}

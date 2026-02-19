#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "hal_ble.h"
#include "error.h"
#include "driver_stats.h"

#define BLE_RX_MAX_LEN 244
#define BLE_RX_Q_LEN 8

LOG_MODULE_REGISTER(ble_nrf, LOG_LEVEL_INF);

struct ble_rx_msg {
    uint16_t len;
    uint8_t data[BLE_RX_MAX_LEN];
};

K_MSGQ_DEFINE(g_ble_rx_q, sizeof(struct ble_rx_msg), BLE_RX_Q_LEN, 4);

static struct bt_conn *g_conn;
static bool g_notify_enabled;
static uint16_t g_att_mtu;
static DRIVER_STATS_DEFINE(g_ble_stats);

/* Custom 128-bit UUIDs (match Android side) */
#define BLE_UUID_SERVICE BT_UUID_128_ENCODE(0xf0debc9a, 0x7856, 0x3412, 0x7856, 0x341234120000)
#define BLE_UUID_RX      BT_UUID_128_ENCODE(0xf1debc9a, 0x7856, 0x3412, 0x7856, 0x341234120000)
#define BLE_UUID_TX      BT_UUID_128_ENCODE(0xf2debc9a, 0x7856, 0x3412, 0x7856, 0x341234120000)

static struct bt_uuid_128 g_ble_svc_uuid = BT_UUID_INIT_128(BLE_UUID_SERVICE);
static struct bt_uuid_128 g_ble_rx_uuid = BT_UUID_INIT_128(BLE_UUID_RX);
static struct bt_uuid_128 g_ble_tx_uuid = BT_UUID_INIT_128(BLE_UUID_TX);

static const uint8_t g_adv_flags[] = { (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR) };
static const uint8_t g_adv_uuid128[] = { BLE_UUID_SERVICE };

static const struct bt_data g_adv_data[] = {
    BT_DATA(BT_DATA_FLAGS, g_adv_flags, sizeof(g_adv_flags)),
    BT_DATA(BT_DATA_UUID128_ALL, g_adv_uuid128, sizeof(g_adv_uuid128)),
};

static const struct bt_le_adv_param g_adv_param = {
    .id = BT_ID_DEFAULT,
    .sid = 0,
    .secondary_max_skip = 0,
    .options = (BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_NAME),
    .interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
    .interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
};

static void auth_cancel(struct bt_conn *conn)
{
    ARG_UNUSED(conn);
    LOG_INF("BLE pairing canceled");
}

static void pairing_confirm(struct bt_conn *conn)
{
    ARG_UNUSED(conn);
    LOG_INF("BLE pairing confirm (Just Works)");
    bt_conn_auth_pairing_confirm(conn);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
    ARG_UNUSED(conn);
    LOG_INF("BLE pairing complete (bonded=%d)", bonded ? 1 : 0);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
    ARG_UNUSED(conn);
    LOG_INF("BLE pairing failed: %d", (int)reason);
}

static struct bt_conn_auth_cb g_auth_cb = {
    .cancel = auth_cancel,
    .pairing_confirm = pairing_confirm,
};

static struct bt_conn_auth_info_cb g_auth_info_cb = {
    .pairing_complete = pairing_complete,
    .pairing_failed = pairing_failed,
};

static ssize_t ble_rx_write(struct bt_conn *conn,
                            const struct bt_gatt_attr *attr,
                            const void *buf,
                            uint16_t len,
                            uint16_t offset,
                            uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(offset);
    ARG_UNUSED(flags);

    if (len == 0 || buf == NULL) {
        driver_stats_record_err(&g_ble_stats, HAL_EINVAL);
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    struct ble_rx_msg msg;
    uint16_t copy_len = MIN(len, BLE_RX_MAX_LEN);
    msg.len = copy_len;
    memcpy(msg.data, buf, copy_len);

    int ret = k_msgq_put(&g_ble_rx_q, &msg, K_NO_WAIT);
    if (ret != 0) {
        driver_stats_record_drop(&g_ble_stats, 1);
        LOG_ERR("BLE RX drop (queue full)");
        return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
    }

    driver_stats_record_ok(&g_ble_stats);
    LOG_INF("BLE RX len=%u", (unsigned int)copy_len);
    LOG_HEXDUMP_INF(msg.data, copy_len, "BLE RX data");
    return len;
}

static void ble_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    g_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("BLE notify %s", g_notify_enabled ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(g_ble_svc,
    BT_GATT_PRIMARY_SERVICE(&g_ble_svc_uuid),
    BT_GATT_CHARACTERISTIC(&g_ble_rx_uuid.uuid,
                           BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, ble_rx_write, NULL),
    BT_GATT_CHARACTERISTIC(&g_ble_tx_uuid.uuid,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE,
                           NULL, NULL, NULL),
    BT_GATT_CCC(ble_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);

static const struct bt_gatt_attr *g_tx_attr = &g_ble_svc.attrs[4];

static void att_mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
    ARG_UNUSED(conn);
    g_att_mtu = tx;
    LOG_INF("BLE ATT MTU updated: tx=%u rx=%u", (unsigned)tx, (unsigned)rx);
}

static struct bt_gatt_cb g_gatt_cb = {
    .att_mtu_updated = att_mtu_updated,
};

static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
                            struct bt_gatt_exchange_params *params)
{
    ARG_UNUSED(params);
    if (err) {
        LOG_ERR("BLE MTU exchange failed: %u", (unsigned)err);
        return;
    }
    uint16_t mtu = bt_gatt_get_mtu(conn);
    g_att_mtu = mtu;
    LOG_INF("BLE MTU exchange ok, mtu=%u", (unsigned)mtu);
}

static struct bt_gatt_exchange_params g_mtu_params = {
    .func = mtu_exchange_cb,
};

static void ble_le_param_updated(struct bt_conn *conn, uint16_t interval,
                                 uint16_t latency, uint16_t timeout)
{
    ARG_UNUSED(conn);
    LOG_INF("BLE conn params: interval=%u units, latency=%u, timeout=%u units",
            (unsigned)interval, (unsigned)latency, (unsigned)timeout);
}

static void ble_connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("BLE connect failed: %u", (unsigned int)err);
        driver_stats_record_err(&g_ble_stats, err);
        return;
    }

    g_conn = bt_conn_ref(conn);
    g_att_mtu = 23;
    LOG_INF("BLE connected");

    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) == 0) {
        if (info.type == BT_CONN_TYPE_LE) {
            LOG_INF("BLE conn info: interval=%u latency=%u timeout=%u",
                    (unsigned)info.le.interval,
                    (unsigned)info.le.latency,
                    (unsigned)info.le.timeout);
        }
    }

    if (IS_ENABLED(CONFIG_BLE_FORCE_SECURITY)) {
        int sec_ret = bt_conn_set_security(conn, BT_SECURITY_L2);
        if (sec_ret) {
            LOG_ERR("BLE set security failed: %d", sec_ret);
        } else {
            LOG_INF("BLE security requested (L2)");
        }
    } else {
        LOG_INF("BLE security not requested");
    }

    if (IS_ENABLED(CONFIG_BT_GATT_CLIENT)) {
        int mtu_ret = bt_gatt_exchange_mtu(conn, &g_mtu_params);
        if (mtu_ret) {
            LOG_ERR("BLE MTU exchange request failed: %d", mtu_ret);
        } else {
            LOG_INF("BLE MTU exchange requested");
        }
    }

    driver_stats_record_ok(&g_ble_stats);
}

static void ble_disconnected(struct bt_conn *conn, uint8_t reason)
{
    ARG_UNUSED(conn);

    LOG_INF("BLE disconnected: %u", (unsigned int)reason);

    if (g_conn) {
        bt_conn_unref(g_conn);
        g_conn = NULL;
    }
    g_notify_enabled = false;
    g_att_mtu = 0;
    driver_stats_record_ok(&g_ble_stats);
}

static struct bt_conn_cb g_conn_cb = {
    .connected = ble_connected,
    .disconnected = ble_disconnected,
    .le_param_updated = ble_le_param_updated,
};

static int ble_nrf_init(void)
{
    driver_stats_init(&g_ble_stats);

    LOG_INF("BLE init");

    int ret = bt_enable(NULL);
    if (ret) {
        driver_stats_record_err(&g_ble_stats, ret);
        LOG_ERR("BLE init failed: %d", ret);
        return ret;
    }

    if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
        settings_load();
        LOG_INF("BLE settings loaded");
    }

    bt_conn_cb_register(&g_conn_cb);
    bt_conn_auth_cb_register(&g_auth_cb);
    bt_conn_auth_info_cb_register(&g_auth_info_cb);
    bt_gatt_cb_register(&g_gatt_cb);

    LOG_INF("BLE init ok");
    return HAL_OK;
}

static int ble_nrf_start(void)
{
    LOG_INF("BLE adv start");

    int ret = bt_le_adv_start(&g_adv_param, g_adv_data, ARRAY_SIZE(g_adv_data), NULL, 0);
    if (ret) {
        driver_stats_record_err(&g_ble_stats, ret);
        LOG_ERR("BLE adv start failed: %d", ret);
        return ret;
    }

    driver_stats_record_ok(&g_ble_stats);
    return HAL_OK;
}

static int ble_nrf_stop(void)
{
    LOG_INF("BLE adv stop");

    int ret = bt_le_adv_stop();
    if (ret) {
        driver_stats_record_err(&g_ble_stats, ret);
        LOG_ERR("BLE adv stop failed: %d", ret);
        return ret;
    }

    driver_stats_record_ok(&g_ble_stats);
    return HAL_OK;
}

static int ble_nrf_send(const void *buf, size_t len, int timeout_ms)
{
    ARG_UNUSED(timeout_ms);

    if (buf == NULL || len == 0) {
        return HAL_EINVAL;
    }
    if (g_conn == NULL || !g_notify_enabled) {
        return HAL_EBUSY;
    }

    if (g_att_mtu > 3) {
        uint16_t max_payload = (uint16_t)(g_att_mtu - 3);
        if (len > max_payload) {
            return -EMSGSIZE;
        }
    }

    int ret = bt_gatt_notify(g_conn, g_tx_attr, buf, (uint16_t)len);
    if (ret) {
        driver_stats_record_err(&g_ble_stats, ret);
        if (ret != -ENOMEM && ret != -EAGAIN) {
            LOG_ERR("BLE TX failed: %d", ret);
        }
        return ret;
    }

    driver_stats_record_ok(&g_ble_stats);
    return HAL_OK;
}

static int ble_nrf_recv(void *buf, size_t len, int timeout_ms)
{
    if (buf == NULL || len == 0) {
        return HAL_EINVAL;
    }

    struct ble_rx_msg msg;
    k_timeout_t timeout = (timeout_ms < 0) ? K_FOREVER : K_MSEC(timeout_ms);
    int ret = k_msgq_get(&g_ble_rx_q, &msg, timeout);
    if (ret != 0) {
        return ret;
    }

    uint16_t copy_len = (msg.len < len) ? msg.len : (uint16_t)len;
    memcpy(buf, msg.data, copy_len);
    if (copy_len < msg.len) {
        driver_stats_record_drop(&g_ble_stats, 1);
    } else {
        driver_stats_record_ok(&g_ble_stats);
    }

    return (int)copy_len;
}

static int ble_nrf_is_ready(void)
{
    return (g_conn != NULL && g_notify_enabled) ? 1 : 0;
}

static int ble_nrf_get_mtu(void)
{
    return (int)g_att_mtu;
}

static const hal_ble_ops_t g_ble_ops = {
    .init = ble_nrf_init,
    .start = ble_nrf_start,
    .stop = ble_nrf_stop,
    .send = ble_nrf_send,
    .recv = ble_nrf_recv,
    .is_ready = ble_nrf_is_ready,
    .get_mtu = ble_nrf_get_mtu,
};

int ble_nrf_register(void)
{
    return hal_ble_register(&g_ble_ops);
}

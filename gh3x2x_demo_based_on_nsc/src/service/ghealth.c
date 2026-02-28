/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include "ghealth.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bt_ghealth);

static struct bt_ghealth_cb ghealth_cb;

static void ghealth_ccc_cfg_changed(const struct bt_gatt_attr *attr,
				  uint16_t value)
{
	if (ghealth_cb.send_enabled) {
		LOG_DBG("Notification has been turned %s",
			value == BT_GATT_CCC_NOTIFY ? "on" : "off");
		ghealth_cb.send_enabled(value == BT_GATT_CCC_NOTIFY ?
			BT_GHEALTH_SEND_STATUS_ENABLED : BT_GHEALTH_SEND_STATUS_DISABLED);
	}
}

static ssize_t on_receive(struct bt_conn *conn,
			  const struct bt_gatt_attr *attr,
			  const void *buf,
			  uint16_t len,
			  uint16_t offset,
			  uint8_t flags)
{
	LOG_DBG("Received data, handle %d, conn %p",
		attr->handle, (void *)conn);

	if (ghealth_cb.received) {
		ghealth_cb.received(conn, buf, len);
}
	return len;
}

static void on_sent(struct bt_conn *conn, void *user_data)
{
	ARG_UGHEALTHED(user_data);

	LOG_DBG("Data send, conn %p", (void *)conn);

	if (ghealth_cb.sent) {
		ghealth_cb.sent(conn);
	}
}

/* UART Service Declaration */
BT_GATT_SERVICE_DEFINE(ghealth_svc,
BT_GATT_PRIMARY_SERVICE(BT_UUID_GHEALTH_SERVICE),
	BT_GATT_CHARACTERISTIC(BT_UUID_GHEALTH_TX,
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       NULL, NULL, NULL),
	BT_GATT_CCC(ghealth_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	
	BT_GATT_CHARACTERISTIC(BT_UUID_GHEALTH_RX,
			       BT_GATT_CHRC_WRITE |
			       BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       NULL, on_receive, NULL),
);

int bt_ghealth_init(struct bt_ghealth_cb *callbacks)
{
	if (callbacks) {
		ghealth_cb.received = callbacks->received;
		ghealth_cb.sent = callbacks->sent;
		ghealth_cb.send_enabled = callbacks->send_enabled;
	}

	return 0;
}

int bt_ghealth_send(struct bt_conn *conn, const uint8_t *data, uint16_t len)
{
	struct bt_gatt_notify_params params = {0};
	const struct bt_gatt_attr *attr = &ghealth_svc.attrs[2];

	params.attr = attr;
	params.data = data;
	params.len = len;
	params.func = on_sent;

	if (!conn) {
		LOG_DBG("Notification send to all connected peers");
		return bt_gatt_notify_cb(NULL, &params);
	} else if (bt_gatt_is_subscribed(conn, attr, BT_GATT_CCC_NOTIFY)) {
		return bt_gatt_notify_cb(conn, &params);
	} else {
		return -EINVAL;
	}
}




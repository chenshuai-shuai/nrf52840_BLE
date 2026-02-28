#ifndef __BLUETOOTH_SERVICES_GHEALTH_H__
#define __BLUETOOTH_SERVICES_GHEALTH_H__

/**
 * @file
 * @defgroup bt_ghealth Nordic UART (GHEALTH) GATT Service
 * @{
 * @brief Nordic UART (GHEALTH) GATT Service API.
 */

#include <zephyr/types.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief UUID of the GHEALTH Service. **/
#define BT_UUID_GHEALTH_VAL \
	BT_UUID_128_ENCODE(0x0000190e, 0x0000, 0x1000, 0x8000, 0x00805f9b34fb)

/** @brief UUID of the TX Characteristic. **/
#define BT_UUID_GHEALTH_TX_VAL \
	BT_UUID_128_ENCODE(0x00000003, 0x0000, 0x1000, 0x8000, 0x00805f9b34fb)

/** @brief UUID of the RX Characteristic. **/
#define BT_UUID_GHEALTH_RX_VAL \
	BT_UUID_128_ENCODE(0x00000004, 0x0000, 0x1000, 0x8000, 0x00805f9b34fb)

#define BT_UUID_GHEALTH_SERVICE   BT_UUID_DECLARE_128(BT_UUID_GHEALTH_VAL)
#define BT_UUID_GHEALTH_RX        BT_UUID_DECLARE_128(BT_UUID_GHEALTH_RX_VAL)
#define BT_UUID_GHEALTH_TX        BT_UUID_DECLARE_128(BT_UUID_GHEALTH_TX_VAL)

/** @brief GHEALTH send status. */
enum bt_ghealth_send_status {
	/** Send notification enabled. */
	BT_GHEALTH_SEND_STATUS_ENABLED,
	/** Send notification disabled. */
	BT_GHEALTH_SEND_STATUS_DISABLED,
};

/** @brief Pointers to the callback functions for service events. */
struct bt_ghealth_cb {
	/** @brief Data received callback.
	 *
	 * The data has been received as a write request on the GHEALTH RX
	 * Characteristic.
	 *
	 * @param[in] conn  Pointer to connection object that has received data.
	 * @param[in] data  Received data.
	 * @param[in] len   Length of received data.
	 */
	void (*received)(struct bt_conn *conn,
			 const uint8_t *const data, uint16_t len);

	/** @brief Data sent callback.
	 *
	 * The data has been sent as a notification and written on the GHEALTH TX
	 * Characteristic.
	 *
	 * @param[in] conn Pointer to connection object, or NULL if sent to all
	 *                 connected peers.
	 */
	void (*sent)(struct bt_conn *conn);

	/** @brief Send state callback.
	 *
	 * Indicate the CCCD descriptor status of the GHEALTH TX characteristic.
	 *
	 * @param[in] status Send notification status.
	 */
	void (*send_enabled)(enum bt_ghealth_send_status status);

};

/**@brief Initialize the service.
 *
 * @details This function registers a GATT service with two characteristics,
 *          TX and RX. A remote device that is connected to this service
 *          can send data to the RX Characteristic. When the remote enables
 *          notifications, it is notified when data is sent to the TX
 *          Characteristic.
 *
 * @param[in] callbacks  Struct with function pointers to callbacks for service
 *                       events. If no callbacks are needed, this parameter can
 *                       be NULL.
 *
 * @retval 0 If initialization is successful.
 *           Otherwise, a negative value is returned.
 */
int bt_ghealth_init(struct bt_ghealth_cb *callbacks);

/**@brief Send data.
 *
 * @details This function sends data to a connected peer, or all connected
 *          peers.
 *
 * @param[in] conn Pointer to connection object, or NULL to send to all
 *                 connected peers.
 * @param[in] data Pointer to a data buffer.
 * @param[in] len  Length of the data in the buffer.
 *
 * @retval 0 If the data is sent.
 *           Otherwise, a negative value is returned.
 */
int bt_ghealth_send(struct bt_conn *conn, const uint8_t *data, uint16_t len);

/**@brief Get maximum data length that can be used for @ref bt_ghealth_send.
 *
 * @param[in] conn Pointer to connection Object.
 *
 * @return Maximum data length.
 */
static inline uint32_t bt_ghealth_get_mtu(struct bt_conn *conn)
{
	/* According to 3.4.7.1 Handle Value Notification off the ATT protocol.
	 * Maximum supported notification is ATT_MTU - 3 */
	return bt_gatt_get_mtu(conn) - 3;
}

#ifdef __cplusplus
}
#endif

/**
 *@}
 */

#endif

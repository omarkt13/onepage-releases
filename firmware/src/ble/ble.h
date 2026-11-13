/* OnePage Mini - Bluetooth: connection, pairing, iOS services. */
#ifndef OP_BLE_H
#define OP_BLE_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/bluetooth/conn.h>

struct bt_gatt_dm;

int ble_init(void);
/* Starts advertising once settings (bonds) are loaded. */
void ble_start(void);
/* Starts the 2-minute pairing window. Removes any bond first. */
void ble_open_pairing_window(void);
bool ble_has_bond(void);
/* Removes every bond and disconnects. */
void ble_remove_bonds(void);
/* Shortest connection interval for transfers (true) or idle (false). */
void ble_set_fast(bool fast);
/* Current connection, or NULL. Not referenced: do not unref. */
struct bt_conn *ble_conn(void);
/* Usable ATT payload per notification (MTU - 3). */
uint16_t ble_att_payload(void);
/* Device ID: low 4 bytes of FICR.DEVICEADDR as 8 hex characters. */
const char *ble_device_id(void);

/* iOS service clients. *_assign returns 0 if the service was found. */
int ancs_setup(void);
int ancs_assign(struct bt_gatt_dm *dm);
void ancs_start(void);
void ancs_reset(void);
/* Negative action (dismiss, or decline a call) for a notification UID. */
int ancs_negative_action(uint32_t uid);

int ams_setup(void);
int ams_assign(struct bt_gatt_dm *dm);
void ams_start(void);
void ams_reset(void);
enum ams_cmd { AMS_TOGGLE, AMS_NEXT, AMS_PREV };
int ams_command(enum ams_cmd cmd);

int cts_setup(void);
int cts_assign(struct bt_gatt_dm *dm);
void cts_start(void);
void cts_reset(void);

/* OnePage GATT service. */
int op_service_init(void);
void op_service_notify_status(void);
void op_service_data_request(uint8_t kind, uint8_t reason);
void op_service_disconnected(void);

int nfc_tag_init(const char *device_id);

#endif /* OP_BLE_H */

/*
 * OnePage Mini - Bluetooth core: advertising (protocol section 3), pairing
 * window, single bond, connection parameters (protocol section 8) and the
 * discovery of the iOS services.
 */
#include <stdio.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <bluetooth/gatt_dm.h>
#include <bluetooth/services/ams_client.h>
#include <bluetooth/services/ancs_client.h>
#include <bluetooth/services/gattp.h>
#include <hal/nrf_ficr.h>

#include "app.h"
#include "ble/ble.h"
#include "ble/op_uuid.h"

LOG_MODULE_REGISTER(ble, LOG_LEVEL_INF);

#define ADV_FAST_INT 160          /* 100 ms in 0.625 ms units */
#define ADV_RECONNECT_INT 1600    /* 1 s */
#define ADV_SLOW_INT 3200         /* 2 s */
#define ADV_RECONNECT_FAST_S 30
#define IDLE_PARAMS_DELAY_S 60

/* Connection intervals in 1.25 ms units, timeout in 10 ms units. */
#define IDLE_INT_MIN 1452 /* 1815 ms */
#define IDLE_INT_MAX 1572 /* 1965 ms */
#define IDLE_TIMEOUT 600  /* 6 s */
#define FAST_INT_MIN 12   /* 15 ms */
#define FAST_INT_MAX 24   /* 30 ms */
#define FAST_TIMEOUT 400  /* 4 s */

#define MFR_COMPANY_ID 0xFFFF /* test ID until a SIG member ID is assigned */
#define MFR_FLAG_PAIRING BIT(0)
#define MFR_FLAG_HAS_BOND BIT(1)

static struct bt_conn *cur_conn;
static char device_id_str[9];
static uint32_t device_id;
static bool pairing_window;
static bool fast_mode;
static int64_t adv_bonded_since;

static struct k_work_delayable adv_work;
static struct k_work_delayable pairing_end_work;
static struct k_work_delayable idle_params_work;
static struct k_work_delayable unsecured_work;

static struct bt_gattp gattp;

/* ---- advertising --------------------------------------------------------- */

static uint8_t mfr_data[7];

static const struct bt_data ad_template[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, OP_UUID_SERVICE_VAL),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, mfr_data, sizeof(mfr_data)),
};

/*
 * Protocol section 3 lists the complete name in the advertising packet, but
 * flags + 128-bit UUID + manufacturer data already use 30 of 31 bytes. The
 * shortened name goes in the scan response with the ANCS solicitation; iOS
 * reads the full name from the GAP Device Name characteristic.
 */
static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_SOLICIT128, BT_UUID_ANCS_VAL),
	BT_DATA(BT_DATA_NAME_SHORTENED, "OnePage", 7),
};

static int bond_count_cb_n;

static void bond_count_cb(const struct bt_bond_info *info, void *user)
{
	ARG_UNUSED(info);
	ARG_UNUSED(user);
	bond_count_cb_n++;
}

bool ble_has_bond(void)
{
	bond_count_cb_n = 0;
	bt_foreach_bond(BT_ID_DEFAULT, bond_count_cb, NULL);
	return bond_count_cb_n > 0;
}

static void adv_stop(void)
{
	(void)bt_le_adv_stop();
}

static int adv_start(uint16_t interval)
{
	struct bt_le_adv_param param = BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONN, interval,
							     interval + interval / 10, NULL);
	uint8_t flags = (pairing_window ? MFR_FLAG_PAIRING : 0) |
			(ble_has_bond() ? MFR_FLAG_HAS_BOND : 0);

	sys_put_le16(MFR_COMPANY_ID, mfr_data);
	sys_put_le32(device_id, mfr_data + 2);
	mfr_data[6] = flags;

	adv_stop();
	int err = bt_le_adv_start(&param, ad_template, ARRAY_SIZE(ad_template), sd,
				  ARRAY_SIZE(sd));

	if (err && err != -EALREADY) {
		LOG_WRN("advertising failed (%d)", err);
	}
	return err;
}

static void adv_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	if (cur_conn) {
		adv_stop();
		return;
	}
	if (pairing_window) {
		adv_start(ADV_FAST_INT);
		return;
	}
	if (!ble_has_bond()) {
		adv_stop();
		LOG_INF("no phone and no pairing window: not advertising");
		return;
	}
	int64_t since = k_uptime_get() - adv_bonded_since;

	if (since < ADV_RECONNECT_FAST_S * 1000) {
		adv_start(ADV_RECONNECT_INT);
		k_work_reschedule(&adv_work, K_MSEC(ADV_RECONNECT_FAST_S * 1000 - since));
	} else {
		adv_start(ADV_SLOW_INT);
	}
}

static void adv_restart(void)
{
	adv_bonded_since = k_uptime_get();
	k_work_reschedule(&adv_work, K_NO_WAIT);
}

/* ---- pairing window -------------------------------------------------------- */

static void pairing_end_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	pairing_window = false;
	app_lock();
	g_app.pairing_window = false;
	app_unlock();
	LOG_INF("pairing window closed");
	app_post(EVT_PAIRING_WINDOW_END, 0, 0);
	adv_restart();
}

void ble_remove_bonds(void)
{
	if (cur_conn) {
		bt_conn_disconnect(cur_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
	int err = bt_unpair(BT_ID_DEFAULT, NULL);

	if (err) {
		LOG_ERR("unpair failed (%d)", err);
	}
	app_lock();
	g_app.bonded = false;
	app_unlock();
}

void ble_open_pairing_window(void)
{
	if (ble_has_bond()) {
		ble_remove_bonds();
	}
	pairing_window = true;
	app_lock();
	g_app.pairing_window = true;
	app_unlock();
	LOG_INF("pairing window open for %d s", CONFIG_ONEPAGE_PAIRING_WINDOW_S);
	k_work_reschedule(&pairing_end_work, K_SECONDS(CONFIG_ONEPAGE_PAIRING_WINDOW_S));
	adv_restart();
}

/* ---- connection parameters ------------------------------------------------ */

static void request_params(bool fast)
{
	if (!cur_conn) {
		return;
	}
	struct bt_le_conn_param p = fast ? (struct bt_le_conn_param)BT_LE_CONN_PARAM_INIT(
						   FAST_INT_MIN, FAST_INT_MAX, 0, FAST_TIMEOUT)
					 : (struct bt_le_conn_param)BT_LE_CONN_PARAM_INIT(
						   IDLE_INT_MIN, IDLE_INT_MAX, 0, IDLE_TIMEOUT);
	int err = bt_conn_le_param_update(cur_conn, &p);

	if (err && err != -EALREADY) {
		LOG_WRN("conn param update failed (%d)", err);
	}
	if (fast) {
		(void)bt_conn_le_phy_update(cur_conn, BT_CONN_LE_PHY_PARAM_2M);
	}
}

static void idle_params_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	if (!fast_mode) {
		request_params(false);
	}
}

bool ble_is_fast(void)
{
	return fast_mode;
}

void ble_set_fast(bool fast)
{
	fast_mode = fast;
	if (fast) {
		k_work_cancel_delayable(&idle_params_work);
		request_params(true);
	} else {
		k_work_reschedule(&idle_params_work, K_SECONDS(5));
	}
}

struct bt_conn *ble_conn(void)
{
	return cur_conn;
}

uint16_t ble_att_payload(void)
{
	if (!cur_conn) {
		return 20;
	}
	uint16_t mtu = bt_gatt_get_mtu(cur_conn);

	return mtu > 3 ? mtu - 3 : 20;
}

const char *ble_device_id(void)
{
	return device_id_str;
}

/* ---- discovery of the iOS services ---------------------------------------- */

enum disc_step { DISC_GATTP, DISC_ANCS, DISC_AMS, DISC_CTS, DISC_DONE };
static enum disc_step disc_step;
static bool ancs_found;
/* Discovery runs once per connection, even if security changes again. */
static bool disc_started;

static void discover_next(struct bt_conn *conn);

static void disc_completed(struct bt_gatt_dm *dm, void *ctx)
{
	ARG_UNUSED(ctx);
	struct bt_conn *conn = bt_gatt_dm_conn_get(dm);
	int err = 0;

	switch (disc_step) {
	case DISC_GATTP:
		err = bt_gattp_handles_assign(dm, &gattp);
		break;
	case DISC_ANCS:
		err = ancs_assign(dm);
		ancs_found = err == 0;
		break;
	case DISC_AMS:
		err = ams_assign(dm);
		break;
	case DISC_CTS:
		err = cts_assign(dm);
		break;
	default:
		break;
	}
	if (err) {
		LOG_WRN("service %d assign failed (%d)", disc_step, err);
	}
	(void)bt_gatt_dm_data_release(dm);

	if (!err) {
		switch (disc_step) {
		case DISC_ANCS: ancs_start(); break;
		case DISC_AMS: ams_start(); break;
		case DISC_CTS: cts_start(); break;
		default: break;
		}
	}
	disc_step++;
	discover_next(conn);
}

static void disc_not_found(struct bt_conn *conn, void *ctx)
{
	ARG_UNUSED(ctx);
	LOG_WRN("service %d not found", disc_step);
	disc_step++;
	discover_next(conn);
}

static void disc_error(struct bt_conn *conn, int err, void *ctx)
{
	ARG_UNUSED(ctx);
	LOG_WRN("discovery %d failed (%d)", disc_step, err);
	disc_step++;
	discover_next(conn);
}

static const struct bt_gatt_dm_cb disc_cb = {
	.completed = disc_completed,
	.service_not_found = disc_not_found,
	.error_found = disc_error,
};

static void service_changed(struct bt_gattp *gp, const struct bt_gattp_handle_range *range,
			    int err)
{
	ARG_UNUSED(range);
	/* iOS may publish ANCS only after bonding; look again. */
	if (!err && !ancs_found && disc_step == DISC_DONE) {
		disc_step = DISC_ANCS;
		discover_next(gp->conn);
	}
}

static void discover_next(struct bt_conn *conn)
{
	static const struct bt_uuid_16 gatt = BT_UUID_INIT_16(BT_UUID_GATT_VAL);
	static const struct bt_uuid_16 cts = BT_UUID_INIT_16(BT_UUID_CTS_VAL);
	static const struct bt_uuid_128 ancs = BT_UUID_INIT_128(BT_UUID_ANCS_VAL);
	static const struct bt_uuid_128 ams = BT_UUID_INIT_128(BT_UUID_AMS_VAL);
	const struct bt_uuid *uuid;

	switch (disc_step) {
	case DISC_GATTP: uuid = &gatt.uuid; break;
	case DISC_ANCS: uuid = &ancs.uuid; break;
	case DISC_AMS: uuid = &ams.uuid; break;
	case DISC_CTS: uuid = &cts.uuid; break;
	default:
		if (gattp.conn) {
			(void)bt_gattp_subscribe_service_changed(&gattp, service_changed);
		}
		LOG_INF("iOS services ready (ANCS %s)", ancs_found ? "yes" : "no");
		app_post(EVT_LINK_READY, 0, 0);
		k_work_reschedule(&idle_params_work, K_SECONDS(IDLE_PARAMS_DELAY_S));
		return;
	}
	int err = bt_gatt_dm_start(conn, uuid, &disc_cb, NULL);

	if (err) {
		LOG_WRN("discovery start %d failed (%d)", disc_step, err);
		disc_step++;
		discover_next(conn);
	}
}

/* ---- connection callbacks ---------------------------------------------------- */

static void unsecured_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	if (cur_conn && bt_conn_get_security(cur_conn) < BT_SECURITY_L2) {
		LOG_WRN("link not encrypted in time: disconnecting");
		bt_conn_disconnect(cur_conn, BT_HCI_ERR_AUTH_FAIL);
	}
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_WRN("connection failed (0x%02x)", err);
		adv_restart();
		return;
	}
	if (cur_conn) {
		bt_conn_disconnect(conn, BT_HCI_ERR_CONN_LIMIT_EXCEEDED);
		return;
	}
	cur_conn = bt_conn_ref(conn);
	LOG_INF("connected");
	k_work_reschedule(&unsecured_work, K_SECONDS(CONFIG_ONEPAGE_UNSECURED_TIMEOUT_S));
	(void)bt_conn_le_data_len_update(conn, BT_LE_DATA_LEN_PARAM_MAX);
	err = bt_conn_set_security(conn, BT_SECURITY_L2);
	if (err) {
		LOG_WRN("set security failed (%d)", err);
	}
	app_post(EVT_CONNECTED, 0, 0);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (conn != cur_conn) {
		return;
	}
	LOG_INF("disconnected (0x%02x)", reason);
	k_work_cancel_delayable(&unsecured_work);
	k_work_cancel_delayable(&idle_params_work);
	bt_conn_unref(cur_conn);
	cur_conn = NULL;
	fast_mode = false;
	disc_step = DISC_GATTP;
	disc_started = false;
	ancs_found = false;
	ancs_reset();
	ams_reset();
	cts_reset();
	op_service_disconnected();
	app_post(EVT_DISCONNECTED, reason, 0);
}

static void recycled(void)
{
	adv_restart();
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	if (err) {
		LOG_WRN("security failed (%d)", err);
		return;
	}
	LOG_INF("security level %d", level);
	if (level >= BT_SECURITY_L2 && conn == cur_conn) {
		k_work_cancel_delayable(&unsecured_work);
		/* A second security change (key refresh) must not start a second
		 * discovery: that would subscribe ANCS twice and send a second
		 * EVT_LINK_READY with its data requests.
		 */
		if (!disc_started) {
			disc_started = true;
			disc_step = DISC_GATTP;
			discover_next(conn);
		}
	}
}

static bool le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(param);
	return true;
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled,
	.security_changed = security_changed,
	.le_param_req = le_param_req,
};

/* Pairing is refused outside the pairing window, so nobody can replace the
 * owner's phone by being nearby (spec section 7).
 */
static enum bt_security_err pairing_accept(struct bt_conn *conn,
					   const struct bt_conn_pairing_feat *const feat)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(feat);
	if (!pairing_window) {
		LOG_WRN("pairing refused: window closed");
		return BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
	}
	return BT_SECURITY_ERR_SUCCESS;
}

static void auth_cancel(struct bt_conn *conn)
{
	bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

static struct bt_conn_auth_cb auth_cb = {
	.pairing_accept = pairing_accept,
	.cancel = auth_cancel,
};

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	ARG_UNUSED(conn);
	LOG_INF("paired, bonded %d", bonded);
	if (bonded) {
		pairing_window = false;
		k_work_cancel_delayable(&pairing_end_work);
		app_lock();
		g_app.pairing_window = false;
		g_app.bonded = true;
		app_unlock();
		app_post(EVT_PAIRING_DONE, 0, 0);
	}
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	LOG_WRN("pairing failed (%d)", reason);
	bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
}

static struct bt_conn_auth_info_cb auth_info_cb = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

/* ---- init ---------------------------------------------------------------- */

int ble_init(void)
{
	int err;

	device_id = nrf_ficr_deviceaddr_get(NRF_FICR, 0);
	snprintf(device_id_str, sizeof(device_id_str), "%08X", (unsigned int)device_id);

	k_work_init_delayable(&adv_work, adv_work_fn);
	k_work_init_delayable(&pairing_end_work, pairing_end_fn);
	k_work_init_delayable(&idle_params_work, idle_params_fn);
	k_work_init_delayable(&unsecured_work, unsecured_fn);

	err = bt_conn_auth_cb_register(&auth_cb);
	if (!err) {
		err = bt_conn_auth_info_cb_register(&auth_info_cb);
	}
	if (err) {
		return err;
	}
	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("bt_enable failed (%d)", err);
		return err;
	}
	err = bt_gattp_init(&gattp);
	if (!err) {
		err = ancs_setup();
	}
	if (!err) {
		err = ams_setup();
	}
	if (!err) {
		err = cts_setup();
	}
	if (!err) {
		err = op_service_init();
	}
	return err;
}

/* Called by main after settings are loaded, so bonds are known. */
void ble_start(void)
{
	app_lock();
	g_app.bonded = ble_has_bond();
	memcpy(g_app.device_id, device_id_str, sizeof(g_app.device_id));
	app_unlock();
	adv_restart();
}

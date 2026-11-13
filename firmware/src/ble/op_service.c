/*
 * OnePage Mini - OnePage GATT service (protocol section 5).
 *
 * Write handlers run in the Bluetooth RX thread. They do short work under
 * app_lock(); replies go out from a separate notify thread, and book blocks
 * go to a transfer thread that writes flash.
 */
#include <string.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#if defined(CONFIG_MCUMGR_GRP_IMG_UPLOAD_CHECK_HOOK)
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#endif
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "app.h"
#include "ble/ble.h"
#include "ble/op_uuid.h"
#include "core/transfer.h"
#include "storage/books.h"

LOG_MODULE_REGISTER(op_svc, LOG_LEVEL_INF);

#define FAST_IDLE_AFTER_S 30
#define NOTIFY_MAX 244
#define BLOCK_MAX 244
#define BOOK_LIST_MAX 16
#define XFER_OP_SUSPEND 0xFF /* internal: the link dropped */

#define UUID_DEF(name, id) static const struct bt_uuid_128 name = BT_UUID_INIT_128(OP_UUID_VAL(id))
UUID_DEF(uuid_service, 0x0001);
UUID_DEF(uuid_status, 0x0002);
UUID_DEF(uuid_settings, 0x0003);
UUID_DEF(uuid_filter, 0x0004);
UUID_DEF(uuid_home, 0x0005);
UUID_DEF(uuid_calendar, 0x0006);
UUID_DEF(uuid_request, 0x0007);
UUID_DEF(uuid_privacy, 0x0008);
UUID_DEF(uuid_xfer_ctrl, 0x0009);
UUID_DEF(uuid_xfer_data, 0x000A);

enum notify_target { N_STATUS, N_FILTER, N_CALENDAR, N_REQUEST, N_XFER, N_COUNT };

struct notify_item {
	uint8_t target;
	uint8_t len;
	uint8_t data[NOTIFY_MAX];
};

struct block_item {
	uint8_t len;
	uint8_t data[BLOCK_MAX];
};

struct ctrl_item {
	struct op_xfer_cmd cmd;
};

K_MSGQ_DEFINE(notify_q, sizeof(struct notify_item), 8, 4);
K_MSGQ_DEFINE(block_q, sizeof(struct block_item), 12, 4);
K_MSGQ_DEFINE(ctrl_q, sizeof(struct ctrl_item), 4, 4);
static K_SEM_DEFINE(xfer_sem, 0, 1);

static const struct bt_gatt_attr *notify_attr[N_COUNT];
static struct k_work_delayable fast_off_work;
static struct xfer xfer;

/* ---- helpers ----------------------------------------------------------------- */

static void queue_notify(enum notify_target t, const uint8_t *data, size_t len)
{
	struct notify_item it = {.target = (uint8_t)t, .len = (uint8_t)MIN(len, NOTIFY_MAX)};

	if (len == 0) {
		return;
	}
	memcpy(it.data, data, it.len);
	if (k_msgq_put(&notify_q, &it, K_NO_WAIT)) {
		LOG_WRN("notify queue full");
	}
}

static void fast_keepalive(void)
{
	k_work_reschedule(&fast_off_work, K_SECONDS(FAST_IDLE_AFTER_S));
}

void op_service_fast_keepalive(void)
{
	if (!ble_is_fast()) {
		ble_set_fast(true);
	}
	fast_keepalive();
}

static void fast_off_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	ble_set_fast(false);
}

static ssize_t reject_offset(uint16_t offset)
{
	return offset ? BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET) : 0;
}

static void parse_error(int rc)
{
	app_set_error(rc == OP_PARSE_BAD_VERSION ? OP_ERR_VERSION : OP_ERR_MALFORMED);
}

/* ---- Status --------------------------------------------------------------------- */

static ssize_t read_status(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			   uint16_t len, uint16_t offset)
{
	uint8_t v[64];
	size_t n = app_status_encode(v, sizeof(v));

	return bt_gatt_attr_read(conn, attr, buf, len, offset, v, n);
}

void op_service_notify_status(void)
{
	uint8_t v[64];
	size_t n = app_status_encode(v, sizeof(v));

	queue_notify(N_STATUS, v, n);
}

/* ---- Settings ------------------------------------------------------------------- */

static ssize_t read_settings(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			     uint16_t len, uint16_t offset)
{
	uint8_t v[48];

	app_lock();
	size_t n = op_settings_encode(&g_app.settings, v, sizeof(v));

	app_unlock();
	return bt_gatt_attr_read(conn, attr, buf, len, offset, v, n);
}

static ssize_t write_settings(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			      const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	if (offset) {
		return reject_offset(offset);
	}
	app_lock();
	int rc = op_settings_apply(&g_app.settings, buf, len);

	app_unlock();
	if (rc) {
		parse_error(rc);
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	app_post(EVT_SETTINGS, 0, 0);
	return len;
}

/* ---- App filter ------------------------------------------------------------------ */

static ssize_t write_filter(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			    const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	uint8_t rsp[NOTIFY_MAX];
	size_t cap = MIN(sizeof(rsp), ble_att_payload());

	if (offset) {
		return reject_offset(offset);
	}
	app_lock();
	size_t n = app_filter_handle(&g_app.filter, buf, len, rsp, cap);
	bool changed = g_app.filter.dirty;

	app_unlock();
	if (n == 0) {
		app_set_error(OP_ERR_MALFORMED);
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	queue_notify(N_FILTER, rsp, n);
	if (changed) {
		app_post(EVT_FILTER, 0, 0);
	}
	return len;
}

/* ---- Home data --------------------------------------------------------------------- */

static ssize_t write_home(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	if (offset) {
		return reject_offset(offset);
	}
	app_lock();
	int rc = op_weather_apply(&g_app.weather, buf, len);

	app_unlock();
	if (rc) {
		parse_error(rc);
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	app_post(EVT_WEATHER, 0, 0);
	return len;
}

/* ---- Calendar sync ----------------------------------------------------------------- */

static ssize_t write_calendar(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			      const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	struct op_cal_cmd c;
	struct cal_result r = {0};
	bool reply = true;

	if (offset) {
		return reject_offset(offset);
	}
	int rc = op_cal_cmd_parse(&c, buf, len);

	if (rc) {
		parse_error(rc);
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	fast_keepalive();
	app_lock();
	switch (c.op) {
	case OP_CAL_BEGIN:
		r = cal_begin(&g_app.cal, c.version, c.total_len, c.crc32);
		break;
	case OP_CAL_DATA:
		r = cal_data(&g_app.cal, c.offset, c.data, c.data_len);
		reply = r.status != OP_CAL_OK; /* only failures are answered */
		break;
	case OP_CAL_COMMIT:
		r = cal_commit(&g_app.cal, c.version);
		break;
	case OP_CAL_CLEAR:
		r = cal_clear(&g_app.cal);
		break;
	default:
		break;
	}
	app_unlock();

	if (r.status != OP_CAL_OK && c.op != OP_CAL_DATA) {
		app_set_error(OP_ERR_CALENDAR);
	}
	if (reply) {
		uint8_t ack[16];
		size_t n = op_cal_ack_encode(r.status, r.version, r.next_offset, ack, sizeof(ack));

		queue_notify(N_CALENDAR, ack, n);
	}
	if (r.changed) {
		app_post(EVT_CALENDAR, 0, 0);
	}
	return len;
}

/* ---- Privacy ----------------------------------------------------------------------- */

static ssize_t read_privacy(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			    uint16_t len, uint16_t offset)
{
	uint8_t v[8];

	app_lock();
	size_t n = op_privacy_encode(&g_app.privacy_cfg, v, sizeof(v));

	app_unlock();
	return bt_gatt_attr_read(conn, attr, buf, len, offset, v, n);
}

static ssize_t write_privacy(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	bool erase;

	if (offset) {
		return reject_offset(offset);
	}
	app_lock();
	int rc = op_privacy_apply(&g_app.privacy_cfg, &erase, buf, len);

	app_unlock();
	if (rc) {
		parse_error(rc);
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	app_post(erase ? EVT_ERASE_ALL : EVT_PRIVACY, 0, 0);
	return len;
}

/* ---- Transfer ------------------------------------------------------------------------- */

static void send_book_list(uint16_t start)
{
	/* Static: about 1.5 KB, too much for the Bluetooth RX thread's stack.
	 * Only the RX thread calls this function, and there is one connection.
	 */
	static struct op_book_info list[BOOK_LIST_MAX];
	static uint8_t rsp[NOTIFY_MAX];
	uint16_t total = books_count();
	uint16_t n = 0;

	for (uint16_t i = start; i < total && n < BOOK_LIST_MAX; i++) {
		if (books_get(i, &list[n])) {
			n++;
		}
	}
	size_t len = op_xfer_list_encode(list, start + n, start, rsp,
					 MIN(sizeof(rsp), ble_att_payload()));
	/* The reply's total is the library size, not start + n. */
	sys_put_le16(total, &rsp[2]);
	queue_notify(N_XFER, rsp, len);
}

static ssize_t write_xfer_ctrl(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			       const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	struct ctrl_item it;

	if (offset) {
		return reject_offset(offset);
	}
	int rc = op_xfer_cmd_parse(&it.cmd, buf, len);

	if (rc) {
		parse_error(rc);
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	switch (it.cmd.op) {
	case OP_XFER_FAST:
		/* Valid at any time, also before a calendar sync. */
		ble_set_fast(it.cmd.fast_on != 0);
		if (it.cmd.fast_on) {
			fast_keepalive();
		} else {
			k_work_cancel_delayable(&fast_off_work);
		}
		return len;
	case OP_XFER_LIST:
		send_book_list(it.cmd.start);
		return len;
	default:
		break;
	}
	fast_keepalive();
	if (k_msgq_put(&ctrl_q, &it, K_MSEC(100))) {
		return BT_GATT_ERR(BT_ATT_ERR_PREPARE_QUEUE_FULL);
	}
	k_sem_give(&xfer_sem);
	return len;
}

static ssize_t write_xfer_data(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			       const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	struct block_item it;

	if (offset || len > BLOCK_MAX) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	it.len = (uint8_t)len;
	memcpy(it.data, buf, len);
	fast_keepalive();
	/* Waiting here slows the link down, which is the flow control we want. */
	if (k_msgq_put(&block_q, &it, K_MSEC(200))) {
		LOG_WRN("block dropped; the device will ask for a resend");
	}
	k_sem_give(&xfer_sem);
	return len;
}

static void ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	ARG_UNUSED(value);
}

#define PERM_R BT_GATT_PERM_READ_ENCRYPT
#define PERM_W BT_GATT_PERM_WRITE_ENCRYPT
#define PERM_CCC (BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT)

BT_GATT_SERVICE_DEFINE(op_svc,
	BT_GATT_PRIMARY_SERVICE(&uuid_service),
	BT_GATT_CHARACTERISTIC(&uuid_status.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       PERM_R, read_status, NULL, NULL),
	BT_GATT_CCC(ccc_changed, PERM_CCC),
	BT_GATT_CHARACTERISTIC(&uuid_settings.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       PERM_R | PERM_W, read_settings, write_settings, NULL),
	BT_GATT_CHARACTERISTIC(&uuid_filter.uuid, BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
			       PERM_W, NULL, write_filter, NULL),
	BT_GATT_CCC(ccc_changed, PERM_CCC),
	BT_GATT_CHARACTERISTIC(&uuid_home.uuid, BT_GATT_CHRC_WRITE, PERM_W, NULL, write_home,
			       NULL),
	BT_GATT_CHARACTERISTIC(&uuid_calendar.uuid, BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
			       PERM_W, NULL, write_calendar, NULL),
	BT_GATT_CCC(ccc_changed, PERM_CCC),
	BT_GATT_CHARACTERISTIC(&uuid_request.uuid, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE, NULL,
			       NULL, NULL),
	BT_GATT_CCC(ccc_changed, PERM_CCC),
	BT_GATT_CHARACTERISTIC(&uuid_privacy.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       PERM_R | PERM_W, read_privacy, write_privacy, NULL),
	BT_GATT_CHARACTERISTIC(&uuid_xfer_ctrl.uuid, BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
			       PERM_W, NULL, write_xfer_ctrl, NULL),
	BT_GATT_CCC(ccc_changed, PERM_CCC),
	BT_GATT_CHARACTERISTIC(&uuid_xfer_data.uuid, BT_GATT_CHRC_WRITE_WITHOUT_RESP, PERM_W,
			       NULL, write_xfer_data, NULL),
);

/* ---- threads ----------------------------------------------------------------------- */

static void notify_thread(void)
{
	struct notify_item it;

	for (;;) {
		k_msgq_get(&notify_q, &it, K_FOREVER);
		struct bt_conn *conn = ble_conn();

		if (!conn || it.target >= N_COUNT || !notify_attr[it.target]) {
			continue;
		}
		int err = bt_gatt_notify(conn, notify_attr[it.target], it.data, it.len);

		if (err && err != -ENOTCONN) {
			LOG_DBG("notify %u failed (%d)", it.target, err);
		}
	}
}

/*
 * Writes queued blocks. With yield_to_ctrl it stops as soon as a command
 * waits, so CANCEL is not held behind a long stream of blocks.
 */
static void drain_blocks(bool yield_to_ctrl)
{
	struct block_item b;
	struct xfer_reply r;
	uint32_t n = k_msgq_num_used_get(&block_q);

	/* Without yielding, write only what was queued on entry. */
	while ((yield_to_ctrl ? k_msgq_num_used_get(&ctrl_q) == 0 : n-- > 0) &&
	       k_msgq_get(&block_q, &b, K_NO_WAIT) == 0) {
		struct op_xfer_block blk;

		if (op_xfer_block_parse(&blk, b.data, b.len) != OP_PARSE_OK) {
			continue;
		}
		xfer_block(&xfer, &blk, &r);
		queue_notify(N_XFER, r.buf, r.len);
	}
}

static void xfer_thread(void)
{
	struct ctrl_item c;
	struct xfer_reply r;

	xfer_init(&xfer, books_xfer_io());
	for (;;) {
		k_sem_take(&xfer_sem, K_FOREVER);
		/* Commands first, so START is seen before its blocks. */
		while (k_msgq_get(&ctrl_q, &c, K_NO_WAIT) == 0) {
			if (c.cmd.op == XFER_OP_SUSPEND) {
				/* Keep the partial book; START with the same id resumes it. */
				xfer_suspend(&xfer);
				ble_set_fast(false);
				continue;
			}
			/*
			 * QUERY and FINISH describe the blocks the app has already
			 * sent, so the blocks queued before them are written first.
			 * Otherwise QUERY reports an old offset and the app resends.
			 */
			if (c.cmd.op == OP_XFER_QUERY || c.cmd.op == OP_XFER_FINISH) {
				drain_blocks(false);
			}
			if (c.cmd.op == OP_XFER_DELETE) {
				/* Stop a transfer of this book first, so no block lands
				 * in a deleted file.
				 */
				bool was_partial = xfer_forget(&xfer, c.cmd.book_id);
				int err = books_delete(c.cmd.book_id);
				uint8_t st = (err == 0 || (err == -ENOENT && was_partial))
						     ? OP_XFER_OK
					     : err == -ENOENT ? OP_XFER_UNKNOWN_BOOK : OP_XFER_STORAGE;

				r.len = op_xfer_result_encode(c.cmd.book_id, st, r.buf, sizeof(r.buf));
				app_post(EVT_BOOKS, 0, 0);
			} else {
				xfer_command(&xfer, &c.cmd, &r);
				if (c.cmd.op == OP_XFER_FINISH) {
					if (r.len && r.buf[6] != OP_XFER_OK) {
						app_set_error(OP_ERR_BOOK);
					}
					app_post(EVT_BOOKS, 0, 0);
				}
			}
			queue_notify(N_XFER, r.buf, r.len);
		}
		drain_blocks(true);
		if (k_msgq_num_used_get(&ctrl_q) || k_msgq_num_used_get(&block_q)) {
			k_sem_give(&xfer_sem);
		}
	}
}

K_THREAD_DEFINE(op_notify_tid, 1536, notify_thread, NULL, NULL, NULL, 6, 0, 0);
K_THREAD_DEFINE(op_xfer_tid, 3072, xfer_thread, NULL, NULL, NULL, 8, 0, 0);

void op_service_data_request(uint8_t kind, uint8_t reason)
{
	uint8_t v[4];
	size_t n = op_data_request_encode(kind, reason, v, sizeof(v));

	queue_notify(N_REQUEST, v, n);
}

void op_service_disconnected(void)
{
	k_work_cancel_delayable(&fast_off_work);
	k_msgq_purge(&notify_q);
	k_msgq_purge(&block_q);
	struct ctrl_item it = {.cmd.op = XFER_OP_SUSPEND};

	(void)k_msgq_put(&ctrl_q, &it, K_NO_WAIT);
	k_sem_give(&xfer_sem);
	/* A calendar staged in part stays: BEGIN with the same version resumes. */
}

#if defined(CONFIG_MCUMGR_GRP_IMG_UPLOAD_CHECK_HOOK)
/*
 * Each firmware-update chunk keeps the link at the fast interval. The app
 * also sends FAST, but an update from another SMP client (or an app that
 * does not) would otherwise run at the 2 s idle interval and take hours.
 */
static enum mgmt_cb_return smp_chunk_cb(uint32_t event, enum mgmt_cb_return prev_status,
					int32_t *rc, uint16_t *group, bool *abort_more,
					void *data, size_t data_size)
{
	ARG_UNUSED(event);
	ARG_UNUSED(rc);
	ARG_UNUSED(group);
	ARG_UNUSED(abort_more);
	ARG_UNUSED(data);
	ARG_UNUSED(data_size);
	op_service_fast_keepalive();
	ARG_UNUSED(prev_status);
	return MGMT_CB_OK; /* observe only; never changes the upload result */
}

static struct mgmt_callback smp_chunk_hook = {
	.callback = smp_chunk_cb,
	.event_id = MGMT_EVT_OP_IMG_MGMT_DFU_CHUNK,
};
#endif

int op_service_init(void)
{
	static const struct bt_uuid *uuids[N_COUNT] = {
		[N_STATUS] = &uuid_status.uuid,     [N_FILTER] = &uuid_filter.uuid,
		[N_CALENDAR] = &uuid_calendar.uuid, [N_REQUEST] = &uuid_request.uuid,
		[N_XFER] = &uuid_xfer_ctrl.uuid,
	};

	k_work_init_delayable(&fast_off_work, fast_off_fn);
#if defined(CONFIG_MCUMGR_GRP_IMG_UPLOAD_CHECK_HOOK)
	mgmt_callback_register(&smp_chunk_hook);
#endif
	for (int i = 0; i < N_COUNT; i++) {
		notify_attr[i] = bt_gatt_find_by_uuid(op_svc.attrs, op_svc.attr_count, uuids[i]);
		if (!notify_attr[i]) {
			return -ENOENT;
		}
	}
	return 0;
}

/*
 * OnePage Mini - Apple Notification Center Service client.
 * Notifications are built from their attributes and kept in RAM only.
 */
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <bluetooth/gatt_dm.h>
#include <bluetooth/services/ancs_client.h>

#include "app.h"
#include "ble/ble.h"
#include "clock.h"
#include "core/text.h"

LOG_MODULE_REGISTER(ancs, LOG_LEVEL_INF);

/* Attribute buffers (bytes of UTF-8, iOS truncates to these). */
#define ATTR_APP_ID_LEN (NOTIF_APP_ID_MAX + 1)
#define ATTR_TITLE_LEN 192
#define ATTR_SUBTITLE_LEN 128
#define ATTR_MESSAGE_LEN NOTIF_MESSAGE_MAX
#define ATTR_DATE_LEN 18
#define ATTR_NAME_LEN 64

#define QUEUE_LEN 16
#define NAME_CACHE 16

/* Event flag bits passed with EVT_NOTIF_ADDED in `a`. */
#define NF_PRE_EXISTING BIT(0)
#define NF_IOS_SILENT BIT(1)
#define NF_NEW BIT(2)

static struct bt_ancs_client ancs_c;
static uint8_t a_app_id[ATTR_APP_ID_LEN];
static uint8_t a_title[ATTR_TITLE_LEN];
static uint8_t a_subtitle[ATTR_SUBTITLE_LEN];
static uint8_t a_message[ATTR_MESSAGE_LEN];
static uint8_t a_date[ATTR_DATE_LEN];
static uint8_t a_name[ATTR_NAME_LEN];

static struct notif_builder builder;
static struct bt_ancs_evt_notif pending[QUEUE_LEN];
static uint8_t q_head, q_len;
static uint32_t action_uid[4];
static uint8_t action_len;
static bool busy;
static bool ready;
static uint8_t cur_flags;

static struct {
	char id[NOTIF_APP_ID_MAX + 1];
	char name[NOTIF_APP_NAME_MAX + 1];
} names[NAME_CACHE];
static uint8_t names_next;
static char name_req_id[NOTIF_APP_ID_MAX + 1];

static K_MUTEX_DEFINE(ancs_lock);
static void pump(void);

/* An action has no data-source reply: the write response ends it. */
static void action_done(struct bt_ancs_client *c, uint8_t err)
{
	ARG_UNUSED(c);
	if (err) {
		LOG_DBG("action refused 0x%02x", err);
	}
	k_mutex_lock(&ancs_lock, K_FOREVER);
	busy = false;
	k_mutex_unlock(&ancs_lock);
	pump();
}

static const char *cached_name(const char *id)
{
	for (int i = 0; i < NAME_CACHE; i++) {
		if (names[i].id[0] && strcmp(names[i].id, id) == 0) {
			return names[i].name;
		}
	}
	return NULL;
}

static void write_done(struct bt_ancs_client *c, uint8_t err)
{
	ARG_UNUSED(c);
	if (err) {
		/* iOS refused (for example the notification is gone): move on. */
		LOG_DBG("control point error 0x%02x", err);
		k_mutex_lock(&ancs_lock, K_FOREVER);
		builder.active = false;
		busy = false;
		k_mutex_unlock(&ancs_lock);
		pump();
	}
}

/* Starts the next request if the control point is free. */
static void pump(void)
{
	int err = 0;

	k_mutex_lock(&ancs_lock, K_FOREVER);
	if (busy || !ready) {
		k_mutex_unlock(&ancs_lock);
		return;
	}
	if (action_len) {
		uint32_t uid = action_uid[0];

		memmove(action_uid, action_uid + 1, sizeof(action_uid[0]) * --action_len);
		busy = true;
		k_mutex_unlock(&ancs_lock);
		err = bt_ancs_notification_action(&ancs_c, uid, BT_ANCS_ACTION_ID_NEGATIVE,
						  action_done);
		if (err) {
			LOG_WRN("action failed (%d)", err);
			k_mutex_lock(&ancs_lock, K_FOREVER);
			busy = false;
			k_mutex_unlock(&ancs_lock);
		}
		return;
	}
	if (q_len == 0) {
		k_mutex_unlock(&ancs_lock);
		return;
	}
	struct bt_ancs_evt_notif n = pending[q_head];

	q_head = (q_head + 1) % QUEUE_LEN;
	q_len--;
	busy = true;
	cur_flags = (n.evt_flags.pre_existing ? NF_PRE_EXISTING : 0) |
		    (n.evt_flags.silent ? NF_IOS_SILENT : 0) |
		    (n.evt_id == BT_ANCS_EVENT_ID_NOTIFICATION_ADDED ? NF_NEW : 0);
	notif_builder_begin(&builder, n.notif_uid, (uint8_t)n.category_id, n.evt_flags.silent,
			    n.evt_flags.positive_action, n.evt_flags.negative_action,
			    clock_now());
	k_mutex_unlock(&ancs_lock);

	err = bt_ancs_request_attrs(&ancs_c, &n, write_done);
	if (err) {
		LOG_WRN("attribute request failed (%d)", err);
		k_mutex_lock(&ancs_lock, K_FOREVER);
		busy = false;
		builder.active = false;
		k_mutex_unlock(&ancs_lock);
	}
}

static void ns_cb(struct bt_ancs_client *c, int err, const struct bt_ancs_evt_notif *n)
{
	ARG_UNUSED(c);
	if (err) {
		return;
	}
	if (n->evt_id == BT_ANCS_EVENT_ID_NOTIFICATION_REMOVED) {
		app_post(EVT_NOTIF_REMOVED, 0, n->notif_uid);
		return;
	}
	k_mutex_lock(&ancs_lock, K_FOREVER);
	if (q_len < QUEUE_LEN) {
		pending[(q_head + q_len) % QUEUE_LEN] = *n;
		q_len++;
	} else {
		LOG_WRN("notification queue full, uid %u dropped", n->notif_uid);
	}
	k_mutex_unlock(&ancs_lock);
	pump();
}

/* A completed notification: apply the app filter, store it, tell the app. */
static void finish_notif(void)
{
	struct notif *n = &builder.n;
	bool hidden;

	app_lock();
	const struct op_filter_entry *f = app_filter_seen(&g_app.filter, n->app_id);
	bool filter_changed = g_app.filter.dirty;

	hidden = f->mode == OP_MODE_HIDE;
	if (!hidden) {
		n->silent = n->silent || f->mode == OP_MODE_SILENT;
		n->title_only = (f->flags & OP_FILTER_FLAG_TITLE_ONLY) != 0;
		const char *name = cached_name(n->app_id);

		if (name) {
			strncpy(n->app_name, name, NOTIF_APP_NAME_MAX);
		}
		notif_list_put(&g_app.notifs, n);
	} else {
		notif_list_remove(&g_app.notifs, n->uid);
	}
	app_unlock();

	if (filter_changed) {
		app_post(EVT_FILTER, 0, 0);
	}
	if (!hidden) {
		app_post(EVT_NOTIF_ADDED, cur_flags, n->uid);
		if (!cached_name(n->app_id) && n->app_id[0]) {
			strncpy(name_req_id, n->app_id, NOTIF_APP_ID_MAX);
			if (bt_ancs_request_app_attr(&ancs_c, (const uint8_t *)name_req_id,
						     strlen(name_req_id), write_done) == 0) {
				return; /* busy until the name arrives */
			}
		}
	}
	k_mutex_lock(&ancs_lock, K_FOREVER);
	busy = false;
	k_mutex_unlock(&ancs_lock);
	pump();
}

static void apply_name(const uint8_t *data, size_t len)
{
	int slot = names_next;

	names_next = (names_next + 1) % NAME_CACHE;
	strncpy(names[slot].id, name_req_id, NOTIF_APP_ID_MAX);
	op_utf8_to_cp1252((const char *)data, len, names[slot].name, sizeof(names[slot].name));

	uint32_t updated = 0;

	app_lock();
	for (uint8_t i = 0; i < notif_list_count(&g_app.notifs); i++) {
		struct notif *n = notif_list_newest(&g_app.notifs, i);

		if (strcmp(n->app_id, name_req_id) == 0) {
			strncpy(n->app_name, names[slot].name, NOTIF_APP_NAME_MAX);
			updated = n->uid;
		}
	}
	app_unlock();
	if (updated) {
		app_post(EVT_NOTIF_ADDED, 0, updated); /* redraw only: flags 0 = no alert */
	}
	k_mutex_lock(&ancs_lock, K_FOREVER);
	busy = false;
	k_mutex_unlock(&ancs_lock);
	pump();
}

static void ds_cb(struct bt_ancs_client *c, const struct bt_ancs_attr_response *rsp)
{
	ARG_UNUSED(c);
	if (rsp->command_id == BT_ANCS_COMMAND_ID_GET_APP_ATTRIBUTES) {
		apply_name(rsp->attr.attr_data, rsp->attr.attr_len);
		return;
	}
	if (rsp->command_id != BT_ANCS_COMMAND_ID_GET_NOTIF_ATTRIBUTES) {
		return;
	}
	if (notif_builder_attr(&builder, rsp->notif_uid, (uint8_t)rsp->attr.attr_id,
			       rsp->attr.attr_data, rsp->attr.attr_len, NOTIF_ATTR_DATE)) {
		finish_notif();
	}
}

int ancs_setup(void)
{
	int err = bt_ancs_client_init(&ancs_c);

	err = err ?: bt_ancs_register_attr(&ancs_c, BT_ANCS_NOTIF_ATTR_ID_APP_IDENTIFIER, a_app_id,
					  sizeof(a_app_id));
	err = err ?: bt_ancs_register_attr(&ancs_c, BT_ANCS_NOTIF_ATTR_ID_TITLE, a_title,
					  sizeof(a_title));
	err = err ?: bt_ancs_register_attr(&ancs_c, BT_ANCS_NOTIF_ATTR_ID_SUBTITLE, a_subtitle,
					  sizeof(a_subtitle));
	err = err ?: bt_ancs_register_attr(&ancs_c, BT_ANCS_NOTIF_ATTR_ID_MESSAGE, a_message,
					  sizeof(a_message));
	err = err ?: bt_ancs_register_attr(&ancs_c, BT_ANCS_NOTIF_ATTR_ID_DATE, a_date,
					  sizeof(a_date));
	err = err ?: bt_ancs_register_app_attr(&ancs_c, BT_ANCS_APP_ATTR_ID_DISPLAY_NAME, a_name,
					      sizeof(a_name));
	return err;
}

int ancs_assign(struct bt_gatt_dm *dm)
{
	return bt_ancs_handles_assign(dm, &ancs_c);
}

void ancs_start(void)
{
	int err = bt_ancs_subscribe_notification_source(&ancs_c, ns_cb);

	if (!err) {
		err = bt_ancs_subscribe_data_source(&ancs_c, ds_cb);
	}
	if (err) {
		LOG_WRN("ANCS subscribe failed (%d)", err);
		return;
	}
	k_mutex_lock(&ancs_lock, K_FOREVER);
	ready = true;
	k_mutex_unlock(&ancs_lock);
	LOG_INF("ANCS subscribed");
}

void ancs_reset(void)
{
	k_mutex_lock(&ancs_lock, K_FOREVER);
	ready = false;
	busy = false;
	q_len = 0;
	action_len = 0;
	memset(&builder, 0, sizeof(builder));
	k_mutex_unlock(&ancs_lock);
}

int ancs_negative_action(uint32_t uid)
{
	k_mutex_lock(&ancs_lock, K_FOREVER);
	if (action_len < ARRAY_SIZE(action_uid)) {
		action_uid[action_len++] = uid;
	}
	k_mutex_unlock(&ancs_lock);
	pump();
	return 0;
}

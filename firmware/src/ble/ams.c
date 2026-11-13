/*
 * OnePage Mini - Apple Media Service client: track details, play state and
 * play/pause/next/previous. Works with any audio app; the OnePage app is
 * not involved. Music details are RAM only and erased when the phone is away.
 */
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <bluetooth/gatt_dm.h>
#include <bluetooth/services/ams_client.h>

#include "app.h"
#include "ble/ble.h"
#include "core/text.h"

LOG_MODULE_REGISTER(ams, LOG_LEVEL_INF);

static struct bt_ams_client ams_c;
static bool ready;

static const enum bt_ams_player_attribute_id player_attrs[] = {
	BT_AMS_PLAYER_ATTRIBUTE_ID_PLAYBACK_INFO,
};
static const enum bt_ams_track_attribute_id track_attrs[] = {
	BT_AMS_TRACK_ATTRIBUTE_ID_ARTIST,
	BT_AMS_TRACK_ATTRIBUTE_ID_ALBUM,
	BT_AMS_TRACK_ATTRIBUTE_ID_TITLE,
};

static void copy_text(char *dst, size_t cap, const uint8_t *src, size_t len)
{
	op_utf8_to_cp1252((const char *)src, len, dst, cap);
}

static void eu_notify(struct bt_ams_client *c, const struct bt_ams_entity_update_notif *n,
		      int err)
{
	ARG_UNUSED(c);
	if (err) {
		return;
	}
	app_lock();
	struct music_state *m = &g_app.music;

	if (n->ent_attr.entity == BT_AMS_ENTITY_ID_PLAYER &&
	    n->ent_attr.attribute.player == BT_AMS_PLAYER_ATTRIBUTE_ID_PLAYBACK_INFO) {
		/* "state,rate,elapsed": state 0 paused, 1 playing, 2 rewind, 3 fast-forward. */
		m->playing = n->len > 0 && n->data[0] != '0';
		m->valid = true;
	} else if (n->ent_attr.entity == BT_AMS_ENTITY_ID_TRACK) {
		switch (n->ent_attr.attribute.track) {
		case BT_AMS_TRACK_ATTRIBUTE_ID_ARTIST:
			copy_text(m->artist, sizeof(m->artist), n->data, n->len);
			break;
		case BT_AMS_TRACK_ATTRIBUTE_ID_ALBUM:
			copy_text(m->album, sizeof(m->album), n->data, n->len);
			break;
		case BT_AMS_TRACK_ATTRIBUTE_ID_TITLE:
			copy_text(m->title, sizeof(m->title), n->data, n->len);
			break;
		default:
			break;
		}
		m->valid = true;
	}
	app_unlock();
	app_post(EVT_MUSIC, 0, 0);
}

static void track_written(struct bt_ams_client *c, uint8_t err)
{
	ARG_UNUSED(c);
	if (err) {
		LOG_WRN("track subscription refused 0x%02x", err);
	}
}

/* Entity Update writes go one at a time: player first, then track. */
static void player_written(struct bt_ams_client *c, uint8_t err)
{
	if (err) {
		LOG_WRN("player subscription refused 0x%02x", err);
	}
	struct bt_ams_entity_attribute_list list = {
		.entity = BT_AMS_ENTITY_ID_TRACK,
		.attribute.track = track_attrs,
		.attribute_count = ARRAY_SIZE(track_attrs),
	};

	err = bt_ams_write_entity_update(c, &list, track_written);
	if (err) {
		LOG_WRN("track subscription failed (%d)", err);
	}
}

int ams_setup(void)
{
	return bt_ams_client_init(&ams_c);
}

int ams_assign(struct bt_gatt_dm *dm)
{
	return bt_ams_handles_assign(dm, &ams_c);
}

void ams_start(void)
{
	int err = bt_ams_subscribe_entity_update(&ams_c, eu_notify);

	if (err) {
		LOG_WRN("AMS subscribe failed (%d)", err);
		return;
	}
	struct bt_ams_entity_attribute_list list = {
		.entity = BT_AMS_ENTITY_ID_PLAYER,
		.attribute.player = player_attrs,
		.attribute_count = ARRAY_SIZE(player_attrs),
	};

	err = bt_ams_write_entity_update(&ams_c, &list, player_written);
	if (err) {
		LOG_WRN("player subscription failed (%d)", err);
		return;
	}
	ready = true;
}

void ams_reset(void)
{
	ready = false;
}

static void cmd_written(struct bt_ams_client *c, uint8_t err)
{
	ARG_UNUSED(c);
	if (err) {
		LOG_DBG("remote command refused 0x%02x", err);
	}
}

int ams_command(enum ams_cmd cmd)
{
	enum bt_ams_remote_command_id id;

	if (!ready) {
		return -ENOTCONN;
	}
	switch (cmd) {
	case AMS_NEXT: id = BT_AMS_REMOTE_COMMAND_ID_NEXT_TRACK; break;
	case AMS_PREV: id = BT_AMS_REMOTE_COMMAND_ID_PREVIOUS_TRACK; break;
	default: id = BT_AMS_REMOTE_COMMAND_ID_TOGGLE_PLAY_PAUSE; break;
	}
	return bt_ams_write_remote_command(&ams_c, id, cmd_written);
}

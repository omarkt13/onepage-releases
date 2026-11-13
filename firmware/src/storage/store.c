#include "storage/store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>

#include "app.h"

LOG_MODULE_REGISTER(store, LOG_LEVEL_INF);

#define CAL_CHUNK 1024
#define CAL_CHUNKS (CAL_MAX_BLOB / CAL_CHUNK)
#define FILTER_PER_CHUNK 16
#define FILTER_CHUNKS (APP_FILTER_MAX / FILTER_PER_CHUNK)
#define POS_MAX 64

struct cal_meta {
	uint8_t bank;
	uint32_t version;
	uint32_t len;
	uint32_t crc;
} __packed;

static struct cal_meta cal_meta;
static bool cal_meta_valid;
static struct {
	uint32_t id;
	uint32_t pos;
} positions[POS_MAX];
static uint8_t positions_n;

/* Buffer for one filter chunk: count + 16 entries of up to 68 bytes. */
static uint8_t filter_buf[1 + FILTER_PER_CHUNK * (4 + OP_APP_ID_MAX)];

/* ---- loading --------------------------------------------------------------------- */

static void load_filter_chunk(const uint8_t *buf, size_t len)
{
	struct op_rd r;
	struct op_filter_entry e;

	op_rd_init(&r, buf, len);
	uint8_t n = op_rd_u8(&r);

	for (uint8_t i = 0; i < n && op_filter_entry_read(&r, &e); i++) {
		app_filter_set(&g_app.filter, &e);
	}
}

static uint32_t away_min;

static int h_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next;

	if (settings_name_steq(name, "set", &next) && !next) {
		struct op_settings s;

		if (len == sizeof(s) && read_cb(cb_arg, &s, sizeof(s)) == sizeof(s)) {
			g_app.settings = s;
		}
		return 0;
	}
	if (settings_name_steq(name, "priv", &next) && !next) {
		struct op_privacy p;

		if (len == sizeof(p) && read_cb(cb_arg, &p, sizeof(p)) == sizeof(p)) {
			g_app.privacy_cfg = p;
		}
		return 0;
	}
	if (settings_name_steq(name, "flt", &next) && next) {
		if (len <= sizeof(filter_buf)) {
			ssize_t n = read_cb(cb_arg, filter_buf, len);

			if (n > 0) {
				load_filter_chunk(filter_buf, (size_t)n);
			}
		}
		return 0;
	}
	if (settings_name_steq(name, "away", &next) && !next) {
		uint32_t min;

		if (len == sizeof(min) && read_cb(cb_arg, &min, sizeof(min)) == sizeof(min)) {
			away_min = min;
		}
		return 0;
	}
	if (settings_name_steq(name, "calm", &next) && !next) {
		if (len == sizeof(cal_meta) &&
		    read_cb(cb_arg, &cal_meta, sizeof(cal_meta)) == sizeof(cal_meta)) {
			cal_meta_valid = cal_meta.len <= CAL_MAX_BLOB && cal_meta.bank <= 1;
		}
		return 0;
	}
	if (settings_name_steq(name, "pos", &next) && next) {
		uint32_t pos;

		if (len == sizeof(pos) && positions_n < POS_MAX &&
		    read_cb(cb_arg, &pos, sizeof(pos)) == sizeof(pos)) {
			positions[positions_n].id = (uint32_t)strtoul(next, NULL, 16);
			positions[positions_n].pos = pos;
			positions_n++;
		}
		return 0;
	}
	return 0; /* calendar chunks are read with a direct loader below */
}

SETTINGS_STATIC_HANDLER_DEFINE(op, "op", NULL, h_set, NULL, NULL);

static int cal_chunk_loader(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg,
			    void *param)
{
	ARG_UNUSED(param);
	if (!key) {
		return 0;
	}
	unsigned long idx = strtoul(key, NULL, 10);

	if (idx < CAL_CHUNKS && len <= CAL_CHUNK) {
		(void)read_cb(cb_arg, g_app.cal.stage + idx * CAL_CHUNK, len);
	}
	return 0;
}

static void load_calendar(void)
{
	char path[16];

	if (!cal_meta_valid) {
		return;
	}
	snprintf(path, sizeof(path), "op/cal/%u", cal_meta.bank);
	memset(g_app.cal.stage, 0, sizeof(g_app.cal.stage));
	(void)settings_load_subtree_direct(path, cal_chunk_loader, NULL);
	if (op_crc32(0, g_app.cal.stage, cal_meta.len) != cal_meta.crc ||
	    !cal_load(&g_app.cal, g_app.cal.stage, cal_meta.len, cal_meta.version)) {
		LOG_WRN("stored calendar damaged: ignored");
		cal_erase(&g_app.cal);
		return;
	}
	LOG_INF("calendar v%u, %u events", cal_meta.version, cal_count(&g_app.cal));
}

int store_init(void)
{
	int err = settings_subsys_init();

	if (err) {
		LOG_ERR("settings init failed (%d)", err);
		return err;
	}
	app_lock();
	err = settings_load();
	load_calendar();
	g_app.filter.dirty = false;
	privacy_set_away_before(&g_app.privacy, (uint64_t)away_min * 60u * 1000u);
	app_unlock();
	return err;
}

/* ---- saving ------------------------------------------------------------------------ */

void store_save_away(uint32_t minutes)
{
	if (minutes != away_min) {
		away_min = minutes;
		(void)settings_save_one("op/away", &minutes, sizeof(minutes));
	}
}

void store_save_settings(void)
{
	struct op_settings s;

	app_lock();
	s = g_app.settings;
	app_unlock();
	(void)settings_save_one("op/set", &s, sizeof(s));
}

void store_save_privacy(void)
{
	struct op_privacy p;

	app_lock();
	p = g_app.privacy_cfg;
	app_unlock();
	(void)settings_save_one("op/priv", &p, sizeof(p));
}

void store_save_filter(void)
{
	char key[12];

	app_lock();
	const struct app_filter *f = &g_app.filter;

	for (int c = 0; c < FILTER_CHUNKS; c++) {
		struct op_wr w;
		uint8_t n = 0;

		op_wr_init(&w, filter_buf, sizeof(filter_buf));
		op_wr_u8(&w, 0);
		for (int i = c * FILTER_PER_CHUNK; i < f->n && i < (c + 1) * FILTER_PER_CHUNK; i++) {
			op_filter_entry_write(&w, &f->e[i]);
			n++;
		}
		filter_buf[0] = n;
		snprintf(key, sizeof(key), "op/flt/%d", c);
		if (n == 0) {
			(void)settings_delete(key);
		} else {
			(void)settings_save_one(key, filter_buf, w.pos);
		}
	}
	g_app.filter.dirty = false;
	app_unlock();
}

void store_save_calendar(void)
{
	struct cal_meta m;
	char key[20];

	/* Saved straight from the store under the lock: RRAM writes take
	 * microseconds per word, and a second 16 KB copy would not fit in RAM.
	 */
	app_lock();
	const uint8_t *blob = g_app.cal.blob;
	uint32_t len = g_app.cal.blob_len;

	m.version = g_app.cal.version;
	m.len = len;
	m.crc = op_crc32(0, blob, len);
	if (cal_meta_valid && m.crc == cal_meta.crc && m.len == cal_meta.len) {
		/* Same content: only the version may differ. */
		if (m.version != cal_meta.version) {
			cal_meta.version = m.version;
			(void)settings_save_one("op/calm", &cal_meta, sizeof(cal_meta));
		}
		app_unlock();
		return;
	}
	m.bank = cal_meta_valid ? !cal_meta.bank : 0;
	for (uint32_t i = 0; i < CAL_CHUNKS; i++) {
		snprintf(key, sizeof(key), "op/cal/%u/%u", m.bank, i);
		if (i * CAL_CHUNK < len) {
			(void)settings_save_one(key, blob + i * CAL_CHUNK,
						MIN(CAL_CHUNK, len - i * CAL_CHUNK));
		} else {
			(void)settings_delete(key);
		}
	}
	/* The switch to the new bank is a single write: a power cut before it
	 * leaves the old calendar in place.
	 */
	if (settings_save_one("op/calm", &m, sizeof(m)) == 0) {
		cal_meta = m;
		cal_meta_valid = true;
	}
	app_unlock();
}

void store_erase_personal(void)
{
	char key[20];

	(void)settings_delete("op/set");
	(void)settings_delete("op/priv");
	(void)settings_delete("op/calm");
	(void)settings_delete("op/away");
	away_min = 0;
	for (int c = 0; c < FILTER_CHUNKS; c++) {
		snprintf(key, sizeof(key), "op/flt/%d", c);
		(void)settings_delete(key);
	}
	for (int b = 0; b < 2; b++) {
		for (int i = 0; i < (int)CAL_CHUNKS; i++) {
			snprintf(key, sizeof(key), "op/cal/%d/%d", b, i);
			(void)settings_delete(key);
		}
	}
	cal_meta_valid = false;
}

/* ---- reading positions ---------------------------------------------------------------- */

uint32_t store_get_position(uint32_t id)
{
	for (int i = 0; i < positions_n; i++) {
		if (positions[i].id == id) {
			return positions[i].pos;
		}
	}
	return 0;
}

void store_set_position(uint32_t id, uint32_t pos)
{
	char key[20];
	int i;

	for (i = 0; i < positions_n && positions[i].id != id; i++) {
	}
	if (i == positions_n) {
		if (positions_n == POS_MAX) {
			return;
		}
		positions_n++;
	} else if (positions[i].pos == pos) {
		return;
	}
	positions[i].id = id;
	positions[i].pos = pos;
	snprintf(key, sizeof(key), "op/pos/%08x", id);
	(void)settings_save_one(key, &pos, sizeof(pos));
}

void store_delete_position(uint32_t id)
{
	char key[20];

	for (int i = 0; i < positions_n; i++) {
		if (positions[i].id == id) {
			positions[i] = positions[--positions_n];
			break;
		}
	}
	snprintf(key, sizeof(key), "op/pos/%08x", id);
	(void)settings_delete(key);
}

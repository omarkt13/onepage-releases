/* Behaviour tests for the portable core. */
#include <stdlib.h>
#include <string.h>

#include "alerts.h"
#include "app_filter.h"
#include "battery_policy.h"
#include "calendar.h"
#include "gesture.h"
#include "notif_list.h"
#include "op_time.h"
#include "privacy.h"
#include "testing.h"
#include "transfer.h"
#include "ui_nav.h"

/* ---- privacy -------------------------------------------------------------- */

static void test_privacy_timing(void)
{
	struct privacy p;

	privacy_init(&p, 0);
	CHECK(privacy_locked(&p));
	CHECK_EQ(privacy_link_up(&p, 1000), PRIV_ACT_UNLOCK);
	CHECK(!privacy_locked(&p));
	CHECK_EQ(privacy_tick(&p, 999999), PRIV_ACT_NONE);

	privacy_link_down(&p, 10000);
	CHECK_EQ(privacy_next_deadline(&p, 10000), PRIV_LOCK_DELAY_MS);
	CHECK_EQ(privacy_tick(&p, 10000 + PRIV_LOCK_DELAY_MS - 1), PRIV_ACT_NONE);
	CHECK(!privacy_locked(&p));
	CHECK_EQ(privacy_tick(&p, 10000 + PRIV_LOCK_DELAY_MS), PRIV_ACT_LOCK);
	CHECK(privacy_locked(&p));
	CHECK_EQ(privacy_tick(&p, 10000 + PRIV_LOCK_DELAY_MS + 5), PRIV_ACT_NONE);
	CHECK(privacy_calendar_visible(&p, true));
	CHECK(!privacy_calendar_visible(&p, false));

	/* Short dropout: back within 2 minutes, nothing happens. */
	privacy_link_up(&p, 200000);
	privacy_link_down(&p, 300000);
	CHECK_EQ(privacy_tick(&p, 300000 + 60000), PRIV_ACT_NONE);
	CHECK_EQ(privacy_link_up(&p, 300000 + 90000), PRIV_ACT_NONE);

	/* Seven days away erases the calendar, once. */
	privacy_link_down(&p, 1000000);
	CHECK_EQ(privacy_tick(&p, 1000000 + PRIV_LOCK_DELAY_MS), PRIV_ACT_LOCK);
	CHECK_EQ(privacy_tick(&p, 1000000 + PRIV_ERASE_DELAY_MS - 1), PRIV_ACT_NONE);
	CHECK_EQ(privacy_tick(&p, 1000000 + PRIV_ERASE_DELAY_MS), PRIV_ACT_ERASE_CAL);
	CHECK_EQ(privacy_tick(&p, 1000000 + PRIV_ERASE_DELAY_MS + 1), PRIV_ACT_NONE);
	CHECK_EQ(privacy_next_deadline(&p, 1000000 + PRIV_ERASE_DELAY_MS + 1), UINT64_MAX);
}

/* A restart while away keeps counting toward the 7-day rule. */
static void test_privacy_restart(void)
{
	struct privacy p;
	const uint64_t before = PRIV_ERASE_DELAY_MS - 3600000ull; /* 6 days 23 h */

	privacy_init(&p, 0);
	privacy_set_away_before(&p, before);
	CHECK_EQ(privacy_away_ms(&p, 1000), before + 1000);
	CHECK_EQ(privacy_tick(&p, 1000), PRIV_ACT_NONE); /* boots locked already */
	CHECK_EQ(privacy_next_deadline(&p, 0), 3600000ull);
	CHECK_EQ(privacy_tick(&p, 3600000ull), PRIV_ACT_ERASE_CAL);

	/* The phone returns: the stored count no longer applies. */
	privacy_init(&p, 0);
	privacy_set_away_before(&p, before);
	privacy_link_up(&p, 10);
	CHECK_EQ(privacy_away_ms(&p, 20), 0);
	privacy_link_down(&p, 100);
	CHECK_EQ(privacy_away_ms(&p, 200), 100);
	CHECK_EQ(privacy_tick(&p, 100 + PRIV_ERASE_DELAY_MS - 1), PRIV_ACT_LOCK);
}

/* ---- transfer ------------------------------------------------------------- */

#define FAKE_MAX 20000
struct fake_fs {
	uint8_t data[FAKE_MAX];
	uint32_t len;
	uint32_t id;
	uint32_t total;
	uint32_t crc;
	bool partial;
	bool committed;
	int fail_write_at;
	int writes;
};

static uint8_t f_open(void *ctx, uint32_t id, uint32_t total, uint32_t crc, const char *title,
		      uint32_t *resume)
{
	struct fake_fs *f = ctx;

	(void)title;
	if (total > FAKE_MAX) {
		return OP_XFER_NO_SPACE;
	}
	if (f->partial && f->id == id && f->total == total && f->crc == crc) {
		*resume = f->len;
		return OP_XFER_OK;
	}
	f->partial = true;
	f->id = id;
	f->total = total;
	f->crc = crc;
	f->len = 0;
	*resume = 0;
	return OP_XFER_OK;
}

static int f_write(void *ctx, uint32_t id, uint32_t off, const uint8_t *d, size_t n)
{
	struct fake_fs *f = ctx;

	if (++f->writes == f->fail_write_at || id != f->id || off != f->len) {
		return -1;
	}
	memcpy(f->data + off, d, n);
	f->len += (uint32_t)n;
	return 0;
}

static int f_crc(void *ctx, uint32_t id, uint32_t *crc)
{
	struct fake_fs *f = ctx;

	(void)id;
	*crc = op_crc32(0, f->data, f->len);
	return 0;
}

static int f_commit(void *ctx, uint32_t id)
{
	struct fake_fs *f = ctx;

	(void)id;
	f->partial = false;
	f->committed = true;
	return 0;
}

static void f_discard(void *ctx, uint32_t id)
{
	struct fake_fs *f = ctx;

	(void)id;
	f->partial = false;
	f->len = 0;
}

static struct fake_fs FS;
static const struct xfer_io FIO = {&FS, f_open, f_write, f_crc, f_commit, f_discard};

static void send_block(struct xfer *x, const uint8_t *src, uint32_t off, size_t n,
		       struct xfer_reply *r, bool corrupt)
{
	uint8_t tmp[64];
	struct op_xfer_block b;

	memcpy(tmp, src + off, n);
	b.offset = off;
	b.crc16 = op_crc16(0xFFFF, tmp, n);
	if (corrupt) {
		tmp[0] ^= 0xFF;
	}
	b.data = tmp;
	b.len = n;
	xfer_block(x, &b, r);
}

static uint8_t reply_status(const struct xfer_reply *r)
{
	return r->buf[6];
}

static uint32_t reply_next(const struct xfer_reply *r)
{
	return op_get_le32(&r->buf[7]);
}

static void test_transfer_resume(void)
{
	static uint8_t book[10000];
	struct xfer x;
	struct xfer_reply r;
	struct op_xfer_cmd c = {0};
	const uint32_t B = 50;

	for (size_t i = 0; i < sizeof(book); i++) {
		book[i] = (uint8_t)(i * 7 + 3);
	}
	memset(&FS, 0, sizeof(FS));
	xfer_init(&x, &FIO);

	/* Block before START is refused once. */
	send_block(&x, book, 0, B, &r, false);
	CHECK(r.len > 0);
	send_block(&x, book, 0, B, &r, false);
	CHECK_EQ(r.len, 0);

	c.op = OP_XFER_START;
	c.book_id = 77;
	c.total_len = sizeof(book);
	c.crc32 = op_crc32(0, book, sizeof(book));
	strcpy(c.title, "Book");
	xfer_command(&x, &c, &r);
	CHECK_EQ(r.buf[1], OP_XFER_PROGRESS);
	CHECK_EQ(reply_status(&r), OP_XFER_OK);
	CHECK_EQ(reply_next(&r), 0);

	/* 31 good blocks: no progress yet; the 32nd reports. */
	uint32_t off = 0;

	for (int i = 0; i < 31; i++, off += B) {
		send_block(&x, book, off, B, &r, false);
		CHECK_EQ(r.len, 0);
	}
	send_block(&x, book, off, B, &r, false);
	off += B;
	CHECK_EQ(reply_status(&r), OP_XFER_OK);
	CHECK_EQ(reply_next(&r), off);

	/* A corrupted block is reported once, then later blocks are dropped silently. */
	send_block(&x, book, off, B, &r, true);
	CHECK_EQ(reply_status(&r), OP_XFER_BAD_BLOCK);
	CHECK_EQ(reply_next(&r), off);
	send_block(&x, book, off + B, B, &r, false);
	CHECK_EQ(r.len, 0);
	/* The app resends from the reported offset. */
	send_block(&x, book, off, B, &r, false);
	off += B;
	CHECK_EQ(FS.len, off);

	/* Link drops; START again resumes at the stored length. */
	xfer_suspend(&x);
	send_block(&x, book, off, B, &r, false);
	CHECK_EQ(FS.len, off);
	xfer_command(&x, &c, &r);
	CHECK_EQ(reply_next(&r), off);

	/* FINISH before the end is refused. */
	struct op_xfer_cmd fin = {.op = OP_XFER_FINISH, .book_id = 77};

	xfer_command(&x, &fin, &r);
	CHECK_EQ(r.buf[1], OP_XFER_RESULT);
	CHECK_EQ(r.buf[6], OP_XFER_BAD_BLOCK);

	while (off < sizeof(book)) {
		uint32_t n = sizeof(book) - off < B ? sizeof(book) - off : B;

		send_block(&x, book, off, n, &r, false);
		off += n;
	}
	CHECK_EQ(reply_next(&r), sizeof(book)); /* completion is reported */
	/* A block past the end is refused. */
	send_block(&x, book, 0, B, &r, false);
	CHECK_EQ(reply_status(&r), OP_XFER_BAD_BLOCK);
	xfer_command(&x, &fin, &r);
	CHECK_EQ(r.buf[6], OP_XFER_OK);
	CHECK(FS.committed);
	CHECK(memcmp(FS.data, book, sizeof(book)) == 0);

	/* Whole-file CRC mismatch discards the file. */
	memset(&FS, 0, sizeof(FS));
	c.crc32 ^= 1;
	xfer_command(&x, &c, &r);
	for (off = 0; off < sizeof(book); off += B) {
		send_block(&x, book, off, B, &r, false);
	}
	xfer_command(&x, &fin, &r);
	CHECK_EQ(r.buf[6], OP_XFER_CRC_MISMATCH);
	CHECK(!FS.committed);
	CHECK(!FS.partial);

	/* Storage error mid-way. */
	memset(&FS, 0, sizeof(FS));
	FS.fail_write_at = 3;
	xfer_command(&x, &c, &r);
	send_block(&x, book, 0, B, &r, false);
	send_block(&x, book, B, B, &r, false);
	send_block(&x, book, 2 * B, B, &r, false);
	CHECK_EQ(reply_status(&r), OP_XFER_STORAGE);

	/* No space. */
	c.total_len = FAKE_MAX + 1;
	xfer_command(&x, &c, &r);
	CHECK_EQ(reply_status(&r), OP_XFER_NO_SPACE);

	/* A START for another book while one is in progress: the newest wins. */
	memset(&FS, 0, sizeof(FS));
	c.total_len = sizeof(book);
	c.crc32 = op_crc32(0, book, sizeof(book));
	xfer_command(&x, &c, &r);
	send_block(&x, book, 0, B, &r, false);
	CHECK_EQ(FS.len, B);
	struct op_xfer_cmd other = c;

	other.book_id = 78;
	xfer_command(&x, &other, &r);
	CHECK_EQ(reply_status(&r), OP_XFER_OK);
	CHECK_EQ(reply_next(&r), 0);
	CHECK_EQ(FS.id, 78);
	/* Blocks for the old book are now refused at the new book's offset. */
	send_block(&x, book, B, B, &r, false);
	CHECK_EQ(reply_status(&r), OP_XFER_BAD_BLOCK);
	CHECK_EQ(reply_next(&r), 0);
}

/* ---- calendar --------------------------------------------------------------- */

static size_t make_blob(uint8_t *b, uint32_t gen, int count, const char *title)
{
	struct op_wr w;

	op_wr_init(&w, b, CAL_MAX_BLOB);
	op_wr_u32(&w, gen);
	op_wr_u16(&w, (uint16_t)count);
	for (int i = 0; i < count; i++) {
		op_wr_u32(&w, 1000u + (uint32_t)i * 3600u);
		op_wr_u32(&w, 1000u + (uint32_t)i * 3600u + 1800u);
		op_wr_u8(&w, i == 0 ? 1 : 0);
		op_wr_u8(&w, (uint8_t)strlen(title));
		op_wr_bytes(&w, title, strlen(title));
		op_wr_u8(&w, 0);
	}
	return w.pos;
}

static struct cal_store C;

static void test_calendar_swap(void)
{
	static uint8_t b1[CAL_MAX_BLOB], b2[CAL_MAX_BLOB];
	size_t l1 = make_blob(b1, 500, 3, "Old");
	size_t l2 = make_blob(b2, 600, 5, "New");
	struct cal_result r;
	struct cal_event e;

	cal_init(&C);
	CHECK(cal_load(&C, b1, (uint32_t)l1, 1));
	CHECK_EQ(cal_count(&C), 3);

	/* Interrupted transfer keeps the old calendar. */
	r = cal_begin(&C, 2, (uint32_t)l2, op_crc32(0, b2, l2));
	CHECK_EQ(r.status, OP_CAL_OK);
	r = cal_data(&C, 0, b2, 20);
	CHECK_EQ(r.status, OP_CAL_OK);
	r = cal_data(&C, 40, b2 + 40, 10); /* gap */
	CHECK_EQ(r.status, OP_CAL_OUT_OF_ORDER);
	CHECK_EQ(r.next_offset, 20);
	r = cal_commit(&C, 2);
	CHECK_EQ(r.status, OP_CAL_CRC_MISMATCH);
	CHECK_EQ(cal_count(&C), 3);
	CHECK_EQ(C.version, 1);
	cal_get(&C, 0, &e);
	CHECK(memcmp(e.title, "Old", 3) == 0);

	/* DATA with no BEGIN. */
	r = cal_data(&C, 0, b2, 10);
	CHECK_EQ(r.status, OP_CAL_NO_BEGIN);

	/* Resume: BEGIN again with the same version continues. */
	cal_begin(&C, 3, (uint32_t)l2, op_crc32(0, b2, l2));
	cal_data(&C, 0, b2, 30);
	r = cal_begin(&C, 3, (uint32_t)l2, op_crc32(0, b2, l2));
	CHECK_EQ(r.next_offset, 30);
	cal_data(&C, 30, b2 + 30, l2 - 30);
	r = cal_commit(&C, 3);
	CHECK_EQ(r.status, OP_CAL_OK);
	CHECK(r.changed);
	CHECK_EQ(cal_count(&C), 5);
	CHECK_EQ(C.version, 3);
	CHECK_EQ(C.generated_at, 600);

	/* Bad content with a good CRC is refused and keeps the current copy. */
	b1[5] = 200; /* count > 100 */
	r = cal_begin(&C, 4, (uint32_t)l1, op_crc32(0, b1, l1));
	cal_data(&C, 0, b1, l1);
	r = cal_commit(&C, 4);
	CHECK_EQ(r.status, OP_CAL_BAD_CONTENT);
	CHECK_EQ(C.version, 3);

	r = cal_begin(&C, 5, CAL_MAX_BLOB + 1, 0);
	CHECK_EQ(r.status, OP_CAL_TOO_LARGE);

	/* Expiry views. */
	CHECK_EQ(cal_first_current(&C, 0), 0);
	CHECK_EQ(cal_first_current(&C, 1000 + 1800), 1);
	CHECK_EQ(cal_current_count(&C, 1000 + 3600 + 1800), 3);
	CHECK(cal_next_event(&C, 0, &e));
	CHECK(!e.all_day);
	CHECK_EQ(e.start, 1000 + 3600);
	CHECK(!cal_next_event(&C, 1000000, &e));

	r = cal_clear(&C);
	CHECK_EQ(cal_count(&C), 0);
}

/* ---- app filter ------------------------------------------------------------ */

static struct app_filter F;

static void test_filter_eviction(void)
{
	char id[32];

	app_filter_init(&F);
	struct op_filter_entry custom = {.mode = OP_MODE_HIDE};

	strcpy(custom.id, "app.0");
	app_filter_set(&F, &custom);
	for (int i = 1; i < APP_FILTER_MAX; i++) {
		snprintf(id, sizeof(id), "app.%d", i);
		app_filter_seen(&F, id);
	}
	CHECK_EQ(F.n, APP_FILTER_MAX);
	/* New app: evicts app.1 (oldest default), keeps customised app.0. */
	app_filter_seen(&F, "new.app");
	CHECK_EQ(F.n, APP_FILTER_MAX);
	CHECK(app_filter_find(&F, "app.0") != NULL);
	CHECK(app_filter_find(&F, "app.1") == NULL);
	CHECK(app_filter_find(&F, "new.app") != NULL);
	CHECK_EQ(app_filter_seen(&F, "app.0")->mode, OP_MODE_HIDE);

	/* All customised: new apps still show, but are not stored. */
	for (int i = 0; i < F.n; i++) {
		F.e[i].flags = OP_FILTER_FLAG_TITLE_ONLY;
	}
	const struct op_filter_entry *e = app_filter_seen(&F, "another");

	CHECK_EQ(e->mode, OP_MODE_SHOW);
	CHECK(app_filter_find(&F, "another") == NULL);
	struct op_filter_entry x = {.mode = OP_MODE_SILENT};

	strcpy(x.id, "yet.another");
	CHECK_EQ(app_filter_set(&F, &x), OP_ERR_STORAGE_FULL);
	CHECK_EQ(app_filter_remove(&F, "app.0"), 0);
	CHECK_EQ(app_filter_set(&F, &x), 0);
	CHECK_EQ(app_filter_find(&F, "yet.another")->mode, OP_MODE_SILENT);
}

/* ---- notifications ------------------------------------------------------------ */

static struct notif_list NL;

static void test_notif_list(void)
{
	struct notif n;
	struct notif_builder b;

	notif_list_init(&NL);
	for (uint32_t i = 1; i <= NOTIF_MAX + 1; i++) {
		memset(&n, 0, sizeof(n));
		n.uid = i;
		snprintf(n.title, sizeof(n.title), "T%u", i);
		notif_list_put(&NL, &n);
	}
	CHECK_EQ(notif_list_count(&NL), NOTIF_MAX);
	CHECK(notif_list_find(&NL, 1) == NULL);
	CHECK_EQ(notif_list_newest(&NL, 0)->uid, NOTIF_MAX + 1);
	CHECK_EQ(notif_list_newest(&NL, NOTIF_MAX - 1)->uid, 2);
	CHECK_EQ(notif_list_unread(&NL), NOTIF_MAX);

	/* Modified: replaced and moved to newest. */
	memset(&n, 0, sizeof(n));
	n.uid = 10;
	strcpy(n.title, "changed");
	notif_list_put(&NL, &n);
	CHECK_EQ(notif_list_count(&NL), NOTIF_MAX);
	CHECK_STR(notif_list_newest(&NL, 0)->title, "changed");
	CHECK(notif_list_remove(&NL, 10));
	CHECK(!notif_list_remove(&NL, 10));
	CHECK_EQ(notif_list_count(&NL), NOTIF_MAX - 1);

	/* Builder converts UTF-8 and completes on the last attribute. */
	notif_builder_begin(&b, 99, NOTIF_CAT_INCOMING_CALL, false, true, true, 1234);
	CHECK(!notif_builder_attr(&b, 99, NOTIF_ATTR_APP_ID, (const uint8_t *)"com.apple.mobilephone", 21,
				  NOTIF_ATTR_DATE));
	CHECK(!notif_builder_attr(&b, 98, NOTIF_ATTR_TITLE, (const uint8_t *)"x", 1, NOTIF_ATTR_DATE));
	CHECK(!notif_builder_attr(&b, 99, NOTIF_ATTR_TITLE, (const uint8_t *)"Ren\xc3\xa9\xe2\x80\x99s", 10,
				  NOTIF_ATTR_DATE));
	CHECK(notif_builder_attr(&b, 99, NOTIF_ATTR_DATE, (const uint8_t *)"20261113T101500", 15,
				 NOTIF_ATTR_DATE));
	CHECK_STR(b.n.title, "Ren\xe9\x92s");
	CHECK_STR(b.n.app_id, "com.apple.mobilephone");
	CHECK_EQ(b.n.received_at, 1234);

	notif_list_clear(&NL);
	CHECK_EQ(notif_list_count(&NL), 0);
	static const struct notif_list zero;

	CHECK(memcmp(&NL, &zero, sizeof(NL)) == 0); /* no text left in RAM */
}

/* ---- gestures ----------------------------------------------------------------- */

static enum gesture run(struct gesture_rec *g, uint32_t *t, const int16_t *pos, int n,
			uint32_t step)
{
	enum gesture last = GESTURE_NONE;

	for (int i = 0; i < n; i++) {
		enum gesture e = gesture_feed(g, *t, true, pos[i]);

		if (e) {
			last = e;
		}
		*t += step;
	}
	enum gesture e = gesture_feed(g, *t, false, 0);

	*t += 500;
	return e ? e : last;
}

static void test_gestures(void)
{
	struct gesture_rec g;
	uint32_t t = 1000;

	gesture_init(&g);
	int16_t tap[] = {500, 505, 498, 510};

	CHECK_EQ(run(&g, &t, tap, 4, 30), GESTURE_TAP);

	int16_t brush[] = {500};

	CHECK_EQ(run(&g, &t, brush, 1, 20), GESTURE_NONE); /* 20 ms: too short */

	int16_t fwd[] = {200, 300, 400, 500, 600};

	CHECK_EQ(run(&g, &t, fwd, 5, 40), GESTURE_SWIPE_FWD);
	int16_t back[] = {700, 600, 500, 400};

	CHECK_EQ(run(&g, &t, back, 4, 40), GESTURE_SWIPE_BACK);

	int16_t wiggle[] = {400, 600, 400, 600, 450};

	CHECK_EQ(run(&g, &t, wiggle, 5, 40), GESTURE_NONE);

	/* Long press fires while held, and the release adds nothing. */
	enum gesture e = GESTURE_NONE;
	uint32_t t0 = t;

	for (int i = 0; i < 30; i++) {
		enum gesture x = gesture_feed(&g, t, true, 300);

		if (x) {
			e = x;
			CHECK(t - t0 >= GESTURE_LONG_MS);
		}
		t += 25;
	}
	CHECK_EQ(e, GESTURE_LONG_PRESS);
	CHECK_EQ(gesture_feed(&g, t, false, 0), GESTURE_NONE);
	t += 500;

	/* Slow drag in a pocket. */
	int16_t slow[60];

	for (int i = 0; i < 60; i++) {
		slow[i] = (int16_t)(100 + i * 10);
	}
	CHECK_EQ(run(&g, &t, slow, 60, 30), GESTURE_NONE);

	/* Second contact inside the refractory window is ignored. */
	CHECK_EQ(run(&g, &t, tap, 4, 30), GESTURE_TAP);
	t -= 500 - 50;
	CHECK_EQ(run(&g, &t, tap, 4, 30), GESTURE_NONE);
}

/* ---- navigation --------------------------------------------------------------- */

static void test_navigation(void)
{
	struct ui_nav n;
	struct ui_ctx c = {.notif_count = 3, .cal_count = 2, .book_count = 1, .cal_visible = true,
			   .detail_pages = 1};
	struct ui_result r;
	enum ui_home_item items[HOME_ITEM_COUNT];

	ui_nav_init(&n);
	CHECK_EQ(ui_home_items(&c, items), 5);
	CHECK_EQ(items[0], HOME_NOTIFICATIONS);

	r = ui_nav_input(&n, &c, UI_IN_TAP);
	CHECK_EQ(n.screen, UI_NOTIF_LIST);
	r = ui_nav_input(&n, &c, UI_IN_FWD);
	CHECK(r.redraw);
	CHECK_EQ(n.sel[UI_NOTIF_LIST], 1);
	r = ui_nav_input(&n, &c, UI_IN_TAP);
	CHECK_EQ(n.screen, UI_NOTIF_DETAIL);
	CHECK_EQ(r.act, UI_ACT_NOTIF_OPENED);
	CHECK_EQ(r.index, 1);
	r = ui_nav_input(&n, &c, UI_IN_TAP);
	CHECK_EQ(r.act, UI_ACT_NOTIF_NEGATIVE);
	CHECK_EQ(r.index, 1);
	CHECK_EQ(n.screen, UI_NOTIF_LIST);
	r = ui_nav_input(&n, &c, UI_IN_LONG);
	CHECK_EQ(n.screen, UI_HOME);

	/* Home -> Music: tap toggles, swipes skip. */
	ui_nav_input(&n, &c, UI_IN_FWD);
	ui_nav_input(&n, &c, UI_IN_FWD);
	ui_nav_input(&n, &c, UI_IN_TAP);
	CHECK_EQ(n.screen, UI_MUSIC);
	CHECK_EQ(ui_nav_input(&n, &c, UI_IN_TAP).act, UI_ACT_MUSIC_TOGGLE);
	CHECK_EQ(ui_nav_input(&n, &c, UI_IN_FWD).act, UI_ACT_MUSIC_NEXT);
	CHECK_EQ(ui_nav_input(&n, &c, UI_IN_BACK).act, UI_ACT_MUSIC_PREV);

	/* Button goes Home from anywhere. */
	ui_nav_input(&n, &c, UI_IN_BTN_SHORT);
	CHECK_EQ(n.screen, UI_HOME);

	/* Library -> reader -> long press back to Library with BOOK_CLOSE. */
	n.sel[UI_HOME] = 3;
	ui_nav_input(&n, &c, UI_IN_TAP);
	CHECK_EQ(n.screen, UI_LIBRARY);
	r = ui_nav_input(&n, &c, UI_IN_TAP);
	CHECK_EQ(r.act, UI_ACT_BOOK_OPEN);
	CHECK_EQ(n.screen, UI_READER);
	CHECK_EQ(ui_nav_input(&n, &c, UI_IN_FWD).act, UI_ACT_PAGE_NEXT);
	r = ui_nav_input(&n, &c, UI_IN_LONG);
	CHECK_EQ(r.act, UI_ACT_BOOK_CLOSE);
	CHECK_EQ(n.screen, UI_LIBRARY);

	/* Power menu from the reader closes the book; cancel returns. */
	ui_nav_input(&n, &c, UI_IN_TAP);
	r = ui_nav_input(&n, &c, UI_IN_BTN_HOLD);
	CHECK_EQ(r.act, UI_ACT_BOOK_CLOSE);
	CHECK_EQ(n.screen, UI_POWER_MENU);
	r = ui_nav_input(&n, &c, UI_IN_TAP); /* default selection is Cancel */
	CHECK_EQ(r.act, UI_ACT_NONE);
	CHECK_EQ(n.screen, UI_LIBRARY);

	/* Settings: erase needs a confirmation. */
	ui_nav_input(&n, &c, UI_IN_BTN_SHORT);
	n.sel[UI_HOME] = 4;
	ui_nav_input(&n, &c, UI_IN_TAP);
	CHECK_EQ(n.screen, UI_SETTINGS);
	n.sel[UI_SETTINGS] = SET_ERASE;
	r = ui_nav_input(&n, &c, UI_IN_TAP);
	CHECK_EQ(n.screen, UI_CONFIRM);
	CHECK_EQ(r.act, UI_ACT_NONE);
	r = ui_nav_input(&n, &c, UI_IN_LONG);
	CHECK_EQ(n.screen, UI_SETTINGS);
	ui_nav_input(&n, &c, UI_IN_TAP);
	r = ui_nav_input(&n, &c, UI_IN_TAP);
	CHECK_EQ(r.act, UI_ACT_ERASE_ALL);
	n.sel[UI_SETTINGS] = SET_ALERT_STRENGTH;
	n.screen = UI_SETTINGS;
	r = ui_nav_input(&n, &c, UI_IN_TAP);
	CHECK_EQ(r.act, UI_ACT_SETTING);
	CHECK_EQ(r.index, SET_ALERT_STRENGTH);

	/* Locking: private screens close; Home shows Calendar, Library, Settings. */
	n.screen = UI_NOTIF_DETAIL;
	c.locked = true;
	CHECK(ui_nav_lock(&n, &c));
	CHECK_EQ(n.screen, UI_HOME);
	CHECK_EQ(ui_home_items(&c, items), 3);
	CHECK_EQ(items[0], HOME_CALENDAR);
	c.cal_visible = false;
	CHECK_EQ(ui_home_items(&c, items), 2);
	CHECK_EQ(items[0], HOME_LIBRARY);
	n.screen = UI_READER;
	CHECK(!ui_nav_lock(&n, &c)); /* books stay readable */
	n.screen = UI_MUSIC;
	ui_nav_input(&n, &c, UI_IN_TAP); /* not allowed while locked: goes Home first */
	CHECK(n.screen != UI_MUSIC);

	/* Refresh keeps indexes valid when the list shrinks. */
	c.locked = false;
	c.cal_visible = true;
	n.screen = UI_NOTIF_DETAIL;
	n.detail = 2;
	c.notif_count = 2;
	CHECK(ui_nav_refresh(&n, &c));
	CHECK_EQ(n.screen, UI_NOTIF_LIST);
}

/* ---- alerts, schedule, time ---------------------------------------------------- */

static void test_alerts_and_schedule(void)
{
	struct op_settings s;
	struct alert_input in = {0};

	op_settings_default(&s);
	s.quiet_on = 1; /* 22:00 to 07:00 */
	CHECK(quiet_active(&s, 23 * 60));
	CHECK(quiet_active(&s, 6 * 60 + 59));
	CHECK(!quiet_active(&s, 7 * 60));
	CHECK(!quiet_active(&s, 12 * 60));
	s.quiet_start = 9 * 60;
	s.quiet_end = 17 * 60;
	CHECK(quiet_active(&s, 12 * 60));
	CHECK(!quiet_active(&s, 18 * 60));

	CHECK_EQ(alert_pattern(&s, 18 * 60, &in), OP_PATTERN_SINGLE);
	CHECK_EQ(alert_pattern(&s, 12 * 60, &in), OP_PATTERN_NONE);
	in.category = NOTIF_CAT_INCOMING_CALL;
	CHECK_EQ(alert_pattern(&s, 18 * 60, &in), OP_PATTERN_LONG);
	in.filter_pattern = OP_PATTERN_DOUBLE;
	CHECK_EQ(alert_pattern(&s, 18 * 60, &in), OP_PATTERN_DOUBLE);
	in.filter_mode = OP_MODE_SILENT;
	CHECK_EQ(alert_pattern(&s, 18 * 60, &in), OP_PATTERN_NONE);
	in.filter_mode = OP_MODE_SHOW;
	in.locked = true;
	CHECK_EQ(alert_pattern(&s, 18 * 60, &in), OP_PATTERN_NONE);

	struct sched sc;
	uint32_t local = op_time_from_civil(2026, 11, 13, 10, 30, 0);

	sched_init(&sc);
	CHECK_EQ(sched_tick(&sc, 0, true, local, 0).kind, 0); /* not connected */
	struct sched_req q = sched_connected(&sc, 100, local);

	CHECK_EQ(q.kind, OP_REQ_BOTH);
	CHECK_EQ(q.reason, OP_REASON_RECONNECT);
	CHECK_EQ(sched_tick(&sc, 200, true, local + 100, local + 3600).kind, 0);
	q = sched_tick(&sc, 100 + 3600, true, local + 3600, local + 7200);
	CHECK_EQ(q.kind, OP_REQ_WEATHER);
	/* Crossing 12:00 asks for the calendar. */
	q = sched_tick(&sc, 100 + 3700, true, op_time_from_civil(2026, 11, 13, 12, 0, 5), 0);
	CHECK_EQ(q.kind, OP_REQ_CALENDAR);
	/* Expired weather is asked for again, but not more than every 10 minutes. */
	q = sched_tick(&sc, 100 + 3600 + 700, true, local + 99999, local);
	CHECK_EQ(q.kind & OP_REQ_WEATHER, OP_REQ_WEATHER);
	CHECK_EQ(q.reason, OP_REASON_EXPIRED);
	q = sched_tick(&sc, 100 + 3600 + 800, true, local + 99999, local);
	CHECK_EQ(q.kind & OP_REQ_WEATHER, 0);
	CHECK(sched_next_s(&sc, 100 + 3600 + 800, true, local) <= SCHED_EXPIRED_RETRY_S);

	struct op_civil cv;
	char buf[32];
	uint32_t t = op_time_from_civil(2026, 11, 13, 14, 5, 9);

	op_time_to_civil(t, &cv);
	CHECK_EQ(cv.year, 2026);
	CHECK_EQ(cv.month, 11);
	CHECK_EQ(cv.day, 13);
	CHECK_EQ(cv.hour, 14);
	CHECK_EQ(cv.minute, 5);
	CHECK_EQ(cv.second, 9);
	CHECK_EQ(cv.weekday, 4); /* Friday */
	op_fmt_clock(t, false, buf, sizeof(buf));
	CHECK_STR(buf, "14:05");
	op_fmt_clock(t, true, buf, sizeof(buf));
	CHECK_STR(buf, "2:05 pm");
	op_fmt_date(t, buf, sizeof(buf));
	CHECK_STR(buf, "Fri 13 Nov");
	CHECK_EQ(op_time_from_civil(2000, 1, 1, 0, 0, 0), 0);
	op_time_to_civil(op_time_from_civil(2028, 2, 29, 23, 59, 59), &cv);
	CHECK_EQ(cv.month, 2);
	CHECK_EQ(cv.day, 29);
}

/* Pass 5: DELETE of the book now being received stops the transfer. */
static void test_transfer_forget(void)
{
	static uint8_t book[200];
	struct xfer x;
	struct xfer_reply r;
	struct op_xfer_cmd c = {0};

	for (size_t i = 0; i < sizeof(book); i++) {
		book[i] = (uint8_t)i;
	}
	memset(&FS, 0, sizeof(FS));
	xfer_init(&x, &FIO);
	c.op = OP_XFER_START;
	c.book_id = 5;
	c.total_len = sizeof(book);
	c.crc32 = op_crc32(0, book, sizeof(book));
	strcpy(c.title, "B");
	xfer_command(&x, &c, &r);
	send_block(&x, book, 0, 50, &r, false);
	CHECK(FS.partial);
	CHECK_EQ(FS.len, 50);

	/* Another id: nothing happens. */
	CHECK(!xfer_forget(&x, 6));
	CHECK(FS.partial);
	/* This id: the partial file is discarded and blocks are refused. */
	CHECK(xfer_forget(&x, 5));
	CHECK(!FS.partial);
	CHECK_EQ(FS.len, 0);
	CHECK(!xfer_forget(&x, 5));
	send_block(&x, book, 50, 50, &r, false);
	CHECK_EQ(FS.len, 0);
	CHECK(r.len == 0 || reply_status(&r) != OP_XFER_OK);
}

/* Pass 11: one low gauge reading never switches the device off. */
static void test_battery_policy(void)
{
	struct batt_policy b;

	batt_policy_init(&b);
	/* A single 0 % reading at start-up, then a normal one: stay on. */
	CHECK_EQ(batt_policy_reading(&b, 0, 0, 0, 0) & BATT_ACT_OFF, 0);
	CHECK_EQ(batt_policy_reading(&b, 0, 80, 3950, 0) & BATT_ACT_OFF, 0);
	/* Two low readings within the confirm time: stay on. */
	batt_policy_init(&b);
	CHECK_EQ(batt_policy_reading(&b, 100, 2, 3500, 0) & BATT_ACT_OFF, 0);
	CHECK_EQ(batt_policy_reading(&b, 100 + BATT_CONFIRM_S - 1, 2, 3500, 0) & BATT_ACT_OFF, 0);
	/* Low for the confirm time: off. */
	CHECK(batt_policy_reading(&b, 100 + BATT_CONFIRM_S, 2, 3500, 0) & BATT_ACT_OFF);
	/* 3 % with a healthy voltage (wrong gauge model): stay on. */
	batt_policy_init(&b);
	CHECK_EQ(batt_policy_reading(&b, 0, 3, 3800, 0) & BATT_ACT_OFF, 0);
	CHECK_EQ(batt_policy_reading(&b, 600, 3, 3800, 0) & BATT_ACT_OFF, 0);
	/* Voltage floor wins over a high percentage. */
	CHECK_EQ(batt_policy_reading(&b, 700, 40, 3290, 0) & BATT_ACT_OFF, 0);
	CHECK(batt_policy_reading(&b, 800, 40, 3290, 0) & BATT_ACT_OFF);
	/* Unknown voltage: the percentage alone decides, still confirmed. */
	batt_policy_init(&b);
	CHECK_EQ(batt_policy_reading(&b, 0, 1, 0, 0) & BATT_ACT_OFF, 0);
	CHECK(batt_policy_reading(&b, 300, 1, 0, 0) & BATT_ACT_OFF);
	/* On the charger: never off, and a recovery resets the low run. */
	batt_policy_init(&b);
	CHECK_EQ(batt_policy_reading(&b, 0, 1, 3200, 1) & BATT_ACT_OFF, 0);
	CHECK_EQ(batt_policy_reading(&b, 300, 1, 3200, 1) & BATT_ACT_OFF, 0);
	CHECK_EQ(batt_policy_reading(&b, 400, 2, 3500, 0) & BATT_ACT_OFF, 0);
	CHECK_EQ(batt_policy_reading(&b, 450, 20, 3900, 0) & BATT_ACT_OFF, 0);
	CHECK_EQ(batt_policy_reading(&b, 500, 2, 3500, 0) & BATT_ACT_OFF, 0);

	/* Warning: once at 10 %, again only after rising above 15 % or charging. */
	batt_policy_init(&b);
	CHECK_EQ(batt_policy_reading(&b, 0, 11, 3700, 0), BATT_ACT_NONE);
	CHECK_EQ(batt_policy_reading(&b, 10, 10, 3680, 0), BATT_ACT_WARN);
	CHECK_EQ(batt_policy_reading(&b, 20, 9, 3670, 0), BATT_ACT_NONE);
	CHECK_EQ(batt_policy_reading(&b, 30, 14, 3700, 0), BATT_ACT_NONE);
	CHECK_EQ(batt_policy_reading(&b, 40, 10, 3680, 0), BATT_ACT_NONE);
	CHECK_EQ(batt_policy_reading(&b, 50, 10, 3680, 1), BATT_ACT_NONE);
	CHECK_EQ(batt_policy_reading(&b, 60, 10, 3680, 0), BATT_ACT_WARN);
}

int main(void)
{
	static const struct test_case cases[] = {
		{"privacy_timing", test_privacy_timing},
		{"privacy_restart", test_privacy_restart},
		{"transfer_resume", test_transfer_resume},
		{"transfer_forget", test_transfer_forget},
		{"calendar_atomic_swap", test_calendar_swap},
		{"filter_eviction", test_filter_eviction},
		{"notif_list", test_notif_list},
		{"gestures", test_gestures},
		{"navigation", test_navigation},
		{"alerts_schedule_time", test_alerts_and_schedule},
		{"battery_policy", test_battery_policy},
	};
	return t_run(cases, (int)(sizeof(cases) / sizeof(cases[0])));
}

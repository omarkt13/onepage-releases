/*
 * OnePage Mini - application thread: handles every event, applies the
 * phone-away rules, schedules data requests, and draws the screen.
 */
#include "app.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include "ble/ble.h"
#include "clock.h"
#include "core/op_time.h"
#include "drivers/epd.h"
#include "drivers/power_hw.h"
#include "power.h"
#include "storage/books.h"
#include "storage/store.h"
#include "ui/gfx.h"
#include "ui/screens.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* Flags carried with EVT_NOTIF_ADDED (see ancs.c). */
#define NF_PRE_EXISTING BIT(0)
#define NF_IOS_SILENT BIT(1)
#define NF_NEW BIT(2)

#define BATTERY_POLL_MS (5 * 60 * 1000)
#define POSITION_SAVE_PAGES 20
#define AWAY_SAVE_STEP_MS (60u * 60u * 1000u)

struct app_state g_app;

K_MSGQ_DEFINE(evt_q, sizeof(struct app_evt), 32, 4);
static K_MUTEX_DEFINE(state_mutex);

static struct sched sched;
static enum ui_screen last_screen = UI_SCREEN_COUNT;
static bool force_full;
static uint32_t last_minute;
static int64_t next_battery_poll;
static bool battery_warned;
static uint16_t pages_since_save;

void app_lock(void)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
}

void app_unlock(void)
{
	k_mutex_unlock(&state_mutex);
}

void app_post(uint8_t type, uint8_t a, uint32_t b)
{
	struct app_evt e = {.type = type, .a = a, .b = b};

	if (k_msgq_put(&evt_q, &e, K_NO_WAIT)) {
		LOG_WRN("event queue full, event %u dropped", type);
	}
}

void app_set_error(uint16_t code)
{
	app_lock();
	g_app.last_error = code;
	app_unlock();
	op_service_notify_status();
}

size_t app_status_encode(uint8_t *buf, size_t cap)
{
	struct op_status s = {0};

	books_storage(&s.storage_free_kib, &s.storage_total_kib);
	app_lock();
	s.battery = g_app.battery.percent;
	s.charging = g_app.battery.charging;
	s.last_error = g_app.last_error;
	s.calendar_version = g_app.cal.version;
	s.calendar_synced_at = g_app.cal.generated_at;
	app_unlock();
	strncpy(s.firmware, CONFIG_ONEPAGE_FW_VERSION, sizeof(s.firmware) - 1);
	s.hw_rev = CONFIG_ONEPAGE_HW_REV;
	return op_status_encode(&s, buf, cap);
}

void app_erase_personal(void)
{
	app_lock();
	cal_erase(&g_app.cal);
	memset(&g_app.weather, 0, sizeof(g_app.weather));
	op_settings_default(&g_app.settings);
	op_privacy_default(&g_app.privacy_cfg);
	app_filter_init(&g_app.filter);
	notif_list_clear(&g_app.notifs);
	memset(&g_app.music, 0, sizeof(g_app.music));
	app_unlock();
	store_erase_personal();
	LOG_INF("personal data erased");
}

static void erase_all_and_restart(void)
{
	app_erase_personal();
	books_erase_all();
	ble_remove_bonds();
	screens_draw_message("Erased", "Restarting.");
	(void)epd_show(gfx_fb, EPD_FULL, 0, EPD_HEIGHT - 1);
	sys_reboot(SYS_REBOOT_COLD);
}

/* ---- rendering ------------------------------------------------------------------------ */

static void make_ctx(struct ui_ctx *c)
{
	uint32_t now = clock_now();

	memset(c, 0, sizeof(*c));
	c->notif_count = notif_list_count(&g_app.notifs);
	c->cal_count = cal_current_count(&g_app.cal, now);
	c->book_count = books_count();
	c->locked = privacy_locked(&g_app.privacy);
	/* Without a valid clock the device cannot tell which events are past
	 * (after a restart away from the phone), so it shows no calendar.
	 */
	c->cal_visible = clock_valid() &&
			 privacy_calendar_visible(&g_app.privacy,
						  g_app.privacy_cfg.show_calendar_away);
	c->detail_pages = screens_detail_pages(&g_app.nav);
}

static void render(void)
{
	struct ui_ctx ctx;
	uint16_t y0 = 0, y1 = EPD_HEIGHT - 1;

	app_lock();
	make_ctx(&ctx);
	ui_nav_refresh(&g_app.nav, &ctx);
	screens_draw(&ctx);
	enum ui_screen screen = g_app.nav.screen;
	uint8_t refresh_mode = g_app.settings.refresh_mode;
	app_unlock();

	if (screen == UI_HOME || screen == UI_CHARGING) {
		last_minute = clock_now() / 60;
	}
	if (!epd_present()) {
		gfx_mark_shown();
		last_screen = screen;
		return;
	}
	bool changed = gfx_changed_rows(&y0, &y1);

	if (!changed && !force_full) {
		return;
	}
	enum epd_mode mode;

	if (force_full) {
		mode = EPD_FULL;
		y0 = 0;
		y1 = EPD_HEIGHT - 1;
	} else if (screen != last_screen) {
		mode = refresh_mode == OP_REFRESH_QUALITY ? EPD_FULL
		       : refresh_mode == OP_REFRESH_FAST  ? EPD_PARTIAL
							  : EPD_FAST;
		if (mode == EPD_PARTIAL) {
			y0 = 0;
			y1 = EPD_HEIGHT - 1;
		}
	} else {
		mode = EPD_PARTIAL;
	}
	if (epd_show(gfx_fb, mode, y0, y1) == 0) {
		gfx_mark_shown();
	} else {
		gfx_forget_shown();
	}
	force_full = false;
	last_screen = screen;
}

/* ---- actions -------------------------------------------------------------------------- */

static void reader_close(void)
{
	if (g_reading.open) {
		store_set_position(g_reading.book_id, reader_position(&g_reading.r));
		books_close();
		g_reading.open = false;
	}
	force_full = true; /* leaving the reader clears ghosting */
}

static void reader_open_index(uint16_t index)
{
	struct op_book_info b;
	struct reader_metrics m;

	reader_close();
	force_full = true;
	if (!books_get(index, &b) || books_open(b.book_id, &g_reading.size) ||
	    opb_parse(&g_reading.info, books_read, NULL, g_reading.size)) {
		LOG_WRN("book %u could not be opened", index);
		books_close();
		app_set_error(OP_ERR_BOOK);
		return;
	}
	screens_reader_metrics(&m);
	reader_open(&g_reading.r, &g_reading.info, books_read, NULL, &m, b.position);
	g_reading.book_id = b.book_id;
	g_reading.open = true;
	pages_since_save = 0;
}

static void page_turned(void)
{
	if (++pages_since_save >= POSITION_SAVE_PAGES) {
		pages_since_save = 0;
		store_set_position(g_reading.book_id, reader_position(&g_reading.r));
	}
}

static void change_setting(uint16_t item)
{
	bool privacy = false;
	uint8_t preview = 0;

	app_lock();
	struct op_settings *s = &g_app.settings;

	switch (item) {
	case SET_ALERT_STRENGTH:
		s->alert_strength = (s->alert_strength + 1) % 4;
		preview = s->alert_strength;
		break;
	case SET_QUIET_HOURS:
		s->quiet_on = !s->quiet_on;
		break;
	case SET_REFRESH_MODE:
		s->refresh_mode = (s->refresh_mode + 1) % 3;
		break;
	case SET_CAL_AWAY:
		g_app.privacy_cfg.show_calendar_away = !g_app.privacy_cfg.show_calendar_away;
		privacy = true;
		break;
	default:
		break;
	}
	app_unlock();
	if (privacy) {
		store_save_privacy();
	} else {
		store_save_settings();
	}
	if (preview) {
		alerts_play(OP_PATTERN_SINGLE, preview);
	}
}

static void open_pairing(void)
{
	app_erase_personal();
	ble_open_pairing_window();
	app_lock();
	ui_nav_show(&g_app.nav, UI_PAIRING);
	app_unlock();
	force_full = true;
}

static void do_action(const struct ui_result *r)
{
	switch (r->act) {
	case UI_ACT_NOTIF_OPENED:
		app_lock();
		struct notif *n = notif_list_newest(&g_app.notifs, (uint8_t)r->index);

		if (n) {
			n->read = true;
		}
		app_unlock();
		break;
	case UI_ACT_NOTIF_NEGATIVE: {
		uint32_t uid = 0;
		bool has_neg = false;

		app_lock();
		struct notif *x = notif_list_newest(&g_app.notifs, (uint8_t)r->index);

		if (x) {
			uid = x->uid;
			has_neg = x->has_negative;
		}
		app_unlock();
		if (has_neg) {
			/* iOS then sends "removed", which takes it off the list. */
			ancs_negative_action(uid);
		}
		break;
	}
	case UI_ACT_MUSIC_TOGGLE: ams_command(AMS_TOGGLE); break;
	case UI_ACT_MUSIC_NEXT: ams_command(AMS_NEXT); break;
	case UI_ACT_MUSIC_PREV: ams_command(AMS_PREV); break;
	case UI_ACT_BOOK_OPEN: reader_open_index(r->index); break;
	case UI_ACT_PAGE_NEXT:
		if (g_reading.open && reader_next(&g_reading.r)) {
			page_turned();
		}
		break;
	case UI_ACT_PAGE_PREV:
		if (g_reading.open && reader_prev(&g_reading.r)) {
			page_turned();
		}
		break;
	case UI_ACT_BOOK_CLOSE: reader_close(); break;
	case UI_ACT_SETTING: change_setting(r->index); break;
	case UI_ACT_PAIR_NEW: open_pairing(); break;
	case UI_ACT_ERASE_ALL: erase_all_and_restart(); break;
	case UI_ACT_POWER_OFF:
		power_off("Off", "Press the side button to switch on.");
		break;
	case UI_ACT_RESTART:
		reader_close();
		sys_reboot(SYS_REBOOT_COLD);
		break;
	case UI_ACT_REQUEST_REFRESH:
		if (g_app.link_ready) {
			op_service_data_request(OP_REQ_BOTH, OP_REASON_USER);
		}
		break;
	default:
		break;
	}
}

static void handle_input(enum ui_input in)
{
	struct ui_ctx ctx;
	struct ui_result r;

	app_lock();
	make_ctx(&ctx);
	r = ui_nav_input(&g_app.nav, &ctx, in);
	app_unlock();
	do_action(&r);
	render();
}

/* ---- phone-away rules ------------------------------------------------------------------- */

static void apply_privacy(unsigned act)
{
	if (act & PRIV_ACT_LOCK) {
		struct ui_ctx ctx;

		LOG_INF("phone away: locking");
		alerts_stop();
		app_lock();
		notif_list_clear(&g_app.notifs);
		memset(&g_app.music, 0, sizeof(g_app.music));
		make_ctx(&ctx);
		ui_nav_lock(&g_app.nav, &ctx);
		app_unlock();
		/* Full refresh, so no trace of a message stays on the panel. */
		force_full = true;
	}
	if (act & PRIV_ACT_ERASE_CAL) {
		LOG_INF("phone away 7 days: erasing calendar and weather");
		app_lock();
		cal_erase(&g_app.cal);
		memset(&g_app.weather, 0, sizeof(g_app.weather));
		app_unlock();
		store_save_calendar();
	}
	if (act & PRIV_ACT_UNLOCK) {
		LOG_INF("phone back");
	}
}

/* ---- events -------------------------------------------------------------------------- */

static void notif_added(uint8_t flags, uint32_t uid)
{
	struct alert_input in = {0};
	uint8_t pattern = OP_PATTERN_NONE;
	uint8_t strength;

	app_lock();
	const struct notif *n = notif_list_find(&g_app.notifs, uid);

	if (n && (flags & NF_NEW)) {
		const struct op_filter_entry *f = app_filter_find(&g_app.filter, n->app_id);

		in.filter_mode = f ? f->mode : OP_MODE_SHOW;
		in.filter_pattern = f ? f->pattern : OP_PATTERN_DEFAULT;
		in.category = n->category;
		in.ios_silent = (flags & NF_IOS_SILENT) != 0;
		in.pre_existing = (flags & NF_PRE_EXISTING) != 0;
		in.locked = privacy_locked(&g_app.privacy);
		pattern = alert_pattern(&g_app.settings, op_time_minute_of_day(clock_now()), &in);
	}
	strength = g_app.settings.alert_strength;
	app_unlock();
	if (pattern != OP_PATTERN_NONE) {
		alerts_play(pattern, strength);
	}
}

static void filter_changed(void)
{
	store_save_filter();
	/* Apply hide and title-only to notifications already on the list. */
	app_lock();
	for (int i = notif_list_count(&g_app.notifs) - 1; i >= 0; i--) {
		struct notif *n = notif_list_newest(&g_app.notifs, (uint8_t)i);
		const struct op_filter_entry *f = app_filter_find(&g_app.filter, n->app_id);

		if (!f) {
			continue;
		}
		if (f->mode == OP_MODE_HIDE) {
			notif_list_remove(&g_app.notifs, n->uid);
		} else {
			n->title_only = (f->flags & OP_FILTER_FLAG_TITLE_ONLY) != 0;
		}
	}
	app_unlock();
}

static void battery_check(void)
{
	if (battery_update() != 0) {
		return;
	}
	app_lock();
	uint8_t pct = g_app.battery.percent;
	uint8_t chg = g_app.battery.charging;
	app_unlock();

	if (chg == OP_CHG_NONE && pct <= BATTERY_OFF_PERCENT) {
		power_off("Charge me", "The battery is empty. Connect the charger to switch on.");
	}
	if (chg == OP_CHG_NONE && pct <= BATTERY_WARN_PERCENT && !battery_warned) {
		battery_warned = true;
		alerts_play(OP_PATTERN_DOUBLE, 1);
	}
	if (pct > BATTERY_WARN_PERCENT + 5 || chg != OP_CHG_NONE) {
		battery_warned = false;
	}
	/* Protocol 5.1: notify Status on a change of percent or charging state. */
	static uint8_t last_pct = 0xFF, last_chg = 0xFF;

	if (pct != last_pct || chg != last_chg) {
		last_pct = pct;
		last_chg = chg;
		op_service_notify_status();
	}
}

static void handle(const struct app_evt *e)
{
	switch (e->type) {
	case EVT_INPUT:
		handle_input((enum ui_input)e->a);
		return;
	case EVT_CONNECTED:
		app_lock();
		g_app.connected = true;
		app_unlock();
		break;
	case EVT_LINK_READY: {
		struct sched_req q;

		app_lock();
		g_app.link_ready = true;
		g_app.bonded = true;
		apply_privacy(privacy_link_up(&g_app.privacy, clock_uptime_ms()));
		app_unlock();
		store_save_away(0);
		q = sched_connected(&sched, clock_uptime_ms() / 1000, clock_now());
		op_service_notify_status();
		op_service_data_request(q.kind, q.reason);
		break;
	}
	case EVT_DISCONNECTED:
		app_lock();
		g_app.connected = false;
		g_app.link_ready = false;
		privacy_link_down(&g_app.privacy, clock_uptime_ms());
		app_unlock();
		sched_disconnected(&sched);
		break;
	case EVT_NOTIF_ADDED:
		notif_added(e->a, e->b);
		break;
	case EVT_NOTIF_REMOVED:
		app_lock();
		notif_list_remove(&g_app.notifs, e->b);
		app_unlock();
		break;
	case EVT_CALENDAR:
		store_save_calendar();
		op_service_notify_status();
		break;
	case EVT_SETTINGS:
		store_save_settings();
		break;
	case EVT_FILTER:
		filter_changed();
		break;
	case EVT_PRIVACY:
		store_save_privacy();
		break;
	case EVT_ERASE_ALL:
		erase_all_and_restart();
		return;
	case EVT_BATTERY:
		battery_check();
		break;
	case EVT_CHARGER:
		battery_check();
		app_lock();
		if (e->a == OP_CHG_CHARGING && g_app.nav.screen == UI_HOME) {
			ui_nav_show(&g_app.nav, UI_CHARGING);
		} else if (e->a == OP_CHG_NONE && g_app.nav.screen == UI_CHARGING) {
			ui_nav_show(&g_app.nav, UI_HOME);
		}
		app_unlock();
		break;
	case EVT_NFC_FIELD:
		/* A tap on a device that has a phone only wakes it (protocol 4). */
		if (!ble_has_bond() && !g_app.pairing_window) {
			LOG_INF("NFC tap with no phone: opening pairing");
			open_pairing();
		}
		break;
	case EVT_PAIRING_DONE:
		app_lock();
		g_app.bonded = true;
		ui_nav_show(&g_app.nav, UI_HOME);
		app_unlock();
		force_full = true;
		break;
	case EVT_PAIRING_WINDOW_END:
	case EVT_MUSIC:
	case EVT_TIME_SET:
	case EVT_WEATHER:
	default:
		break;
	}
	render();
}

/* Periodic work. Returns the time until it is next needed. */
static k_timeout_t periodic(void)
{
	uint64_t now_ms = clock_uptime_ms();
	uint32_t now = clock_now();
	uint64_t wait_ms = BATTERY_POLL_MS;

	app_lock();
	unsigned act = privacy_tick(&g_app.privacy, now_ms);
	uint64_t pd = privacy_next_deadline(&g_app.privacy, now_ms);
	uint32_t wx_until = g_app.weather.valid ? g_app.weather.valid_until : 0;
	enum ui_screen screen = g_app.nav.screen;
	uint64_t away_ms = privacy_away_ms(&g_app.privacy, now_ms);
	app_unlock();

	/* Record time away once an hour, so a restart does not reset the 7-day rule. */
	if (away_ms >= AWAY_SAVE_STEP_MS) {
		store_save_away((uint32_t)((away_ms / AWAY_SAVE_STEP_MS) * (AWAY_SAVE_STEP_MS / 60000u)));
		wait_ms = MIN(wait_ms, AWAY_SAVE_STEP_MS - away_ms % AWAY_SAVE_STEP_MS);
	}
	if (act) {
		apply_privacy(act);
		render();
	}
	wait_ms = MIN(wait_ms, pd);

	struct sched_req q = sched_tick(&sched, now_ms / 1000, clock_valid(), now, wx_until);

	if (q.kind) {
		op_service_data_request(q.kind, q.reason);
	}
	uint32_t sn = sched_next_s(&sched, now_ms / 1000, clock_valid(), now);

	if (sn != UINT32_MAX) {
		wait_ms = MIN(wait_ms, (uint64_t)sn * 1000);
	}

	/* The clock on Home changes once a minute. */
	if (clock_valid() && (screen == UI_HOME || screen == UI_CHARGING)) {
		if (now / 60 != last_minute) {
			render();
		}
		wait_ms = MIN(wait_ms, (uint64_t)(60 - now % 60) * 1000 + 200);
	}
	if (k_uptime_get() >= next_battery_poll) {
		next_battery_poll = k_uptime_get() + BATTERY_POLL_MS;
		battery_check();
	}
	return K_MSEC(MAX(wait_ms, 100));
}

void app_init_state(void)
{
	memset(&g_app, 0, sizeof(g_app));
	op_settings_default(&g_app.settings);
	op_privacy_default(&g_app.privacy_cfg);
	privacy_init(&g_app.privacy, clock_uptime_ms());
	notif_list_init(&g_app.notifs);
	cal_init(&g_app.cal);
	app_filter_init(&g_app.filter);
	ui_nav_init(&g_app.nav);
	sched_init(&sched);
}

void app_run(void)
{
	struct app_evt e;

	app_lock();
	if (!g_app.bonded) {
		ui_nav_show(&g_app.nav, UI_PAIRING);
	} else if (charger_state() == OP_CHG_CHARGING) {
		ui_nav_show(&g_app.nav, UI_CHARGING);
	}
	app_unlock();
	battery_check();
	force_full = true;
	render();

	for (;;) {
		k_timeout_t wait = periodic();

		if (k_msgq_get(&evt_q, &e, wait) == 0) {
			handle(&e);
		}
	}
}

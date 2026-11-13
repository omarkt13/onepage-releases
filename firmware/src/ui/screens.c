/*
 * OnePage Mini - screens. Portrait, 528 x 792. All strings are
 * Windows-1252 (the fonts' character set).
 */
#include "ui/screens.h"

#include <stdio.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include "app.h"
#include "clock.h"
#include "core/op_time.h"
#include "core/text.h"
#include "drivers/epd.h"
#include "storage/books.h"
#include "ui/gfx.h"

#define M 28                    /* side margin */
#define CW (GFX_W - 2 * M)      /* content width */
#define HEADER_H 60
#define FOOTER_Y (GFX_H - 44)
#define ROW_PAD 10
#define DETAIL_TOP (HEADER_H + 20)
#define DETAIL_LINES ((FOOTER_Y - DETAIL_TOP - 90) / 26)

struct reading g_reading;

static const char *const wx_names[OP_WX_COUNT] = {
	"", "Clear", "Partly cloudy", "Cloudy", "Fog", "Drizzle", "Rain", "Heavy rain",
	"Snow", "Sleet", "Thunderstorms", "Windy", "Hot", "Cold",
};

/* ---- shared parts ------------------------------------------------------------------- */

static void fmt_temp(char *buf, size_t cap, int16_t tenths_c, uint8_t unit)
{
	int v = unit ? (tenths_c * 9 / 5 + 320) : tenths_c;
	int whole = (v >= 0 ? v + 5 : v - 5) / 10;

	snprintf(buf, cap, "%d\xb0", whole); /* 0xB0 = degree sign */
}

static void header(const char *title)
{
	char buf[48];
	uint32_t now = clock_now();

	gfx_text(&font_ui_bold, M, 14, title, strlen(title));
	if (now) {
		op_fmt_clock(now, g_app.settings.clock_12h, buf, sizeof(buf));
	} else {
		buf[0] = 0;
	}
	if (g_app.battery.gauge_ok) {
		size_t n = strlen(buf);

		snprintf(buf + n, sizeof(buf) - n, "%s%u%%", n ? "  " : "", g_app.battery.percent);
	}
	gfx_text_right(&font_ui, GFX_W - M, 16, buf);
	gfx_hline(M, HEADER_H, CW, 2);
}

static void footer(const char *hint)
{
	gfx_hline(M, FOOTER_Y - 8, CW, 1);
	gfx_text_fit(&font_ui, M, FOOTER_Y, hint, CW);
}

/* Keeps the selected row inside a window of `visible` rows. */
static uint16_t first_row(uint16_t sel, uint16_t visible)
{
	return sel >= visible ? (uint16_t)(sel - visible + 1) : 0;
}

static void draw_list_item(int y, int h, bool selected, const char *text, const char *right)
{
	if (selected) {
		gfx_fill(M - 8, y - 4, CW + 16, h, true);
	}
	int rw = right ? gfx_text_width(&font_ui, right, strlen(right)) + 16 : 0;

	gfx_text_fit(&font_ui_bold, M, y + (h - font_ui_bold.height) / 2 - 4, text, CW - rw);
	if (right) {
		gfx_text_right(&font_ui, GFX_W - M, y + (h - font_ui.height) / 2 - 4, right);
	}
}

/* ---- Home --------------------------------------------------------------------------- */

static void draw_home(const struct ui_ctx *ctx)
{
	char buf[220], t[16], hi[16], lo[16];
	uint32_t now = clock_now();
	int y = 24;

	/* Status line: battery and phone. */
	if (g_app.battery.gauge_ok) {
		snprintf(buf, sizeof(buf), "%u%%%s", g_app.battery.percent,
			 g_app.battery.charging == OP_CHG_CHARGING ? " charging" : "");
		gfx_text(&font_ui, M, y, buf, strlen(buf));
	}
	const char *link = g_app.pairing_window ? "Pairing"
			   : g_app.connected   ? "Connected"
			   : ctx->locked       ? "Phone not nearby"
					       : "Connecting";
	gfx_text_right(&font_ui, GFX_W - M, y, link);
	y += font_ui.height + 10;

	/* Clock and date. */
	if (now) {
		op_fmt_clock(now, g_app.settings.clock_12h, buf, sizeof(buf));
		gfx_text_center(&font_clock, y, buf);
		y += font_clock.height;
		op_fmt_date_long(now, buf, sizeof(buf));
		gfx_text_center(&font_ui_bold, y, buf);
	} else {
		y += 30;
		gfx_text_center(&font_title, y, "--:--");
		y += font_clock.height - 30;
		gfx_text_center(&font_ui, y, "Time comes from your iPhone");
	}
	y += font_ui_bold.height + 14;

	/* Weather, if still valid. */
	if (g_app.weather.valid && now && now < g_app.weather.valid_until) {
		const struct op_weather *w = &g_app.weather;

		fmt_temp(t, sizeof(t), w->temp_now, w->unit);
		fmt_temp(hi, sizeof(hi), w->temp_high, w->unit);
		fmt_temp(lo, sizeof(lo), w->temp_low, w->unit);
		snprintf(buf, sizeof(buf), "%s %s   H %s  L %s", t,
			 w->condition < OP_WX_COUNT ? wx_names[w->condition] : "", hi, lo);
		gfx_text_center(&font_ui, y, buf);
		y += font_ui.height;
		if (w->place[0]) {
			char place[32];

			op_utf8_to_cp1252(w->place, strlen(w->place), place, sizeof(place));
			gfx_text_center(&font_ui, y, place);
		}
		y += font_ui.height;
	} else {
		y += 2 * font_ui.height;
	}

	/* Next event. */
	struct cal_event e;

	if (ctx->cal_visible && now && cal_next_event(&g_app.cal, now, &e)) {
		char title[CAL_TITLE_MAX_BYTES + 1];

		op_utf8_to_cp1252(e.title, e.title_len, title, sizeof(title));
		if (e.all_day) {
			snprintf(buf, sizeof(buf), "Today: %s", title);
		} else {
			op_fmt_clock(e.start, g_app.settings.clock_12h, t, sizeof(t));
			snprintf(buf, sizeof(buf), "%s %s %s",
				 op_time_day(e.start) == op_time_day(now) ? "Next:" : "Next",
				 t, title);
		}
		gfx_text_fit(&font_ui, M, y + 6, buf, CW);
	}
	y += font_ui.height + 16;
	gfx_hline(M, y, CW, 2);
	y += 14;

	/* Menu. */
	enum ui_home_item items[HOME_ITEM_COUNT];
	uint8_t n = ui_home_items(ctx, items);
	const int row_h = 58;

	for (uint8_t i = 0; i < n; i++) {
		const char *label = "";
		char right[24] = "";

		switch (items[i]) {
		case HOME_NOTIFICATIONS:
			label = "Notifications";
			if (notif_list_unread(&g_app.notifs)) {
				snprintf(right, sizeof(right), "%u new", notif_list_unread(&g_app.notifs));
			}
			break;
		case HOME_CALENDAR: label = "Calendar"; break;
		case HOME_MUSIC: label = "Music"; break;
		case HOME_LIBRARY: label = "Library"; break;
		case HOME_SETTINGS: label = "Settings"; break;
		default: break;
		}
		draw_list_item(y, row_h, g_app.nav.sel[UI_HOME] == i, label, right[0] ? right : NULL);
		y += row_h;
	}

	/* Now playing: the shortcut line to the Music screen. */
	if (!ctx->locked && g_app.music.valid && g_app.music.playing && g_app.music.title[0]) {
		snprintf(buf, sizeof(buf), "Now playing: %s", g_app.music.title);
		gfx_text_fit(&font_ui, M, FOOTER_Y, buf, CW);
	} else if (ctx->locked) {
		footer("Notifications return when the phone is back.");
	}
}

/* ---- Notifications ------------------------------------------------------------------ */

static void fmt_age(uint32_t at, char *buf, size_t cap)
{
	if (!at) {
		buf[0] = 0;
		return;
	}
	op_fmt_clock(at, g_app.settings.clock_12h, buf, cap);
}

static void draw_notif_list(void)
{
	const int row_h = 3 * font_ui.height + ROW_PAD;
	const uint16_t visible = (FOOTER_Y - HEADER_H - 20) / row_h;
	uint8_t n = notif_list_count(&g_app.notifs);
	uint16_t sel = g_app.nav.sel[UI_NOTIF_LIST];
	uint16_t first = first_row(sel, visible);
	int y = HEADER_H + 14;
	char t[16];

	header("Notifications");
	if (n == 0) {
		gfx_text_center(&font_ui, GFX_H / 2 - 20, "No notifications");
		return;
	}
	for (uint16_t i = first; i < n && i < first + visible; i++) {
		const struct notif *x = notif_list_newest(&g_app.notifs, (uint8_t)i);
		const char *app = x->app_name[0] ? x->app_name : x->app_id;

		if (i == sel) {
			gfx_fill(M - 8, y - 4, CW + 16, row_h - 2, true);
		}
		fmt_age(x->received_at, t, sizeof(t));
		gfx_text_fit(&font_ui_bold, M, y - 2, app, CW - 120);
		gfx_text_right(&font_ui, GFX_W - M, y, t);
		if (!x->read) {
			gfx_fill(GFX_W - M - 140, y + 10, 10, 10, true);
		}
		if (x->title_only) {
			gfx_text(&font_ui, M, y + font_ui.height, "New notification", 16);
		} else {
			gfx_text_fit(&font_ui, M, y + font_ui.height, x->title, CW);
			const char *body = x->message[0] ? x->message : x->subtitle;
			char first_line[96];
			size_t k = 0;

			while (body[k] && body[k] != '\n' && k < sizeof(first_line) - 1) {
				first_line[k] = body[k];
				k++;
			}
			first_line[k] = 0;
			gfx_text_fit(&font_ui, M, y + 2 * font_ui.height, first_line, CW);
		}
		y += row_h;
	}
	if (n > visible) {
		char pos[16];

		snprintf(pos, sizeof(pos), "%u of %u", sel + 1, n);
		footer(pos);
	}
}

static int notif_detail_lines(const struct notif *x)
{
	return gfx_wrap(&font_ui, M, 0, CW, x->message, strlen(x->message), 1 << 14, 0);
}

static void draw_notif_detail(void)
{
	const struct notif *x = notif_list_newest(&g_app.notifs, (uint8_t)g_app.nav.detail);
	char t[16];
	int y = DETAIL_TOP;

	if (!x) {
		return;
	}
	const char *app = x->app_name[0] ? x->app_name : x->app_id;

	header(app);
	fmt_age(x->received_at, t, sizeof(t));
	gfx_text(&font_ui, M, y, t, strlen(t));
	y += font_ui.height + 6;
	if (x->title_only) {
		gfx_text(&font_ui_bold, M, y, "Content hidden", 14);
		gfx_text(&font_ui, M, y + font_ui_bold.height + 6,
			 "Title-only is set for this app.", 31);
	} else {
		int tl = gfx_wrap(&font_ui_bold, M, y, CW, x->title, strlen(x->title), 0, 2);

		y += MIN(tl, 2) * font_ui_bold.height;
		if (x->subtitle[0]) {
			gfx_text_fit(&font_ui, M, y, x->subtitle, CW);
			y += font_ui.height;
		}
		y += 8;
		int lines = (FOOTER_Y - 16 - y) / font_ui.height;

		gfx_wrap(&font_ui, M, y, CW, x->message, strlen(x->message),
			 g_app.nav.detail_page * DETAIL_LINES, MIN(lines, DETAIL_LINES));
	}
	bool call = x->category == NOTIF_CAT_INCOMING_CALL;

	footer(call ? "Tap: decline   Hold: back"
	       : x->has_negative ? "Tap: dismiss on phone   Hold: back"
				 : "Hold: back");
}

/* ---- Calendar ---------------------------------------------------------------------- */

static void event_time(const struct cal_event *e, char *buf, size_t cap)
{
	char a[16], b[16];

	if (e->all_day) {
		snprintf(buf, cap, "All day");
		return;
	}
	op_fmt_clock(e->start, g_app.settings.clock_12h, a, sizeof(a));
	op_fmt_clock(e->end, g_app.settings.clock_12h, b, sizeof(b));
	snprintf(buf, cap, "%s-%s", a, b);
}

static void draw_calendar(void)
{
	uint32_t now = clock_now();
	uint16_t n = cal_current_count(&g_app.cal, now);
	uint16_t sel = g_app.nav.sel[UI_CALENDAR];
	const int row_h = 2 * font_ui.height + ROW_PAD;
	int y = HEADER_H + 12;
	uint32_t day = UINT32_MAX;
	char buf[200], title[CAL_TITLE_MAX_BYTES + 1];

	header("Calendar");
	if (n == 0) {
		gfx_text_center(&font_ui, GFX_H / 2 - 40, "No events this week");
		gfx_text_center(&font_ui, GFX_H / 2, "Events come from the OnePage app");
	}
	/* Start a few rows before the selection so it stays visible. */
	uint16_t start = sel > 3 ? sel - 3 : 0;

	for (uint16_t i = start; i < n && y < FOOTER_Y - row_h; i++) {
		struct cal_event e;

		cal_get(&g_app.cal, cal_current_nth(&g_app.cal, now, i), &e);
		uint32_t d = op_time_day(e.start < now && now ? now : e.start);

		if (d != day) {
			day = d;
			op_fmt_date_long(d * OP_SECS_PER_DAY, buf, sizeof(buf));
			gfx_text(&font_ui_bold, M, y, buf, strlen(buf));
			y += font_ui_bold.height + 4;
		}
		if (i == sel) {
			gfx_fill(M - 8, y - 4, CW + 16, row_h - 2, true);
		}
		event_time(&e, buf, sizeof(buf));
		int tw = gfx_text(&font_ui, M, y, buf, strlen(buf));

		op_utf8_to_cp1252(e.title, e.title_len, title, sizeof(title));
		gfx_text_fit(&font_ui, MAX(tw + 16, M + 170), y, title, GFX_W - M - MAX(tw + 16, M + 170));
		if (e.place_len) {
			op_utf8_to_cp1252(e.place, e.place_len, title, sizeof(title));
			gfx_text_fit(&font_ui, M + 170, y + font_ui.height, title, CW - 170);
		}
		y += row_h;
	}
	if (g_app.cal.generated_at) {
		char t[16];

		op_fmt_clock(g_app.cal.generated_at, g_app.settings.clock_12h, t, sizeof(t));
		op_fmt_date(g_app.cal.generated_at, title, sizeof(title));
		snprintf(buf, sizeof(buf), "Synced %s %s", title, t);
		footer(buf);
	}
}

static void draw_cal_detail(void)
{
	uint32_t now = clock_now();
	struct cal_event e;
	char buf[200], text[CAL_TITLE_MAX_BYTES + 1];
	int y = DETAIL_TOP;

	header("Event");
	if (!cal_get(&g_app.cal, cal_current_nth(&g_app.cal, now, g_app.nav.detail), &e)) {
		return;
	}
	op_utf8_to_cp1252(e.title, e.title_len, text, sizeof(text));
	y += gfx_wrap(&font_title, M, y, CW, text, strlen(text), 0, 4) * font_title.height + 12;
	op_fmt_date_long(e.start, buf, sizeof(buf));
	gfx_text(&font_ui_bold, M, y, buf, strlen(buf));
	y += font_ui_bold.height;
	event_time(&e, buf, sizeof(buf));
	gfx_text(&font_ui, M, y, buf, strlen(buf));
	y += font_ui.height + 12;
	if (e.place_len) {
		op_utf8_to_cp1252(e.place, e.place_len, text, sizeof(text));
		gfx_wrap(&font_ui, M, y, CW, text, strlen(text), 0, 4);
	}
	footer("Tap or hold: back");
}

/* ---- Music --------------------------------------------------------------------------- */

static void draw_music(void)
{
	const struct music_state *m = &g_app.music;
	int y = HEADER_H + 120;

	header("Music");
	if (!m->valid || !m->title[0]) {
		gfx_text_center(&font_ui, GFX_H / 2 - 40, "Nothing playing");
		gfx_text_center(&font_ui, GFX_H / 2, "Start music on your iPhone");
	} else {
		y += gfx_wrap(&font_title, M, y, CW, m->title, strlen(m->title), 0, 3) *
		     font_title.height + 16;
		gfx_text_fit(&font_ui_bold, M, y, m->artist, CW);
		y += font_ui_bold.height + 4;
		gfx_text_fit(&font_ui, M, y, m->album, CW);
		y += font_ui.height + 40;
		gfx_text_center(&font_title, y, m->playing ? "Playing" : "Paused");
	}
	footer("Tap: play/pause   Swipe: next/previous");
}

/* ---- Library and reader --------------------------------------------------------------- */

static void draw_library(void)
{
	const int row_h = 64;
	const uint16_t visible = (FOOTER_Y - HEADER_H - 20) / row_h;
	uint16_t n = books_count();
	uint16_t sel = g_app.nav.sel[UI_LIBRARY];
	uint16_t first = first_row(sel, visible);
	int y = HEADER_H + 14;

	header("Library");
	if (!books_ready()) {
		gfx_text_center(&font_ui, GFX_H / 2 - 20, "Book storage not available");
		return;
	}
	if (n == 0) {
		gfx_text_center(&font_ui, GFX_H / 2 - 40, "No books");
		gfx_text_center(&font_ui, GFX_H / 2, "Add books from the OnePage app");
		return;
	}
	for (uint16_t i = first; i < n && i < first + visible; i++) {
		struct op_book_info b;
		char title[OP_BOOK_TITLE_MAX + 1], pct[8];

		if (!books_get(i, &b)) {
			break;
		}
		op_utf8_to_cp1252(b.title, strlen(b.title), title, sizeof(title));
		snprintf(pct, sizeof(pct), "%u%%",
			 b.size ? (unsigned)((uint64_t)b.position * 100 / b.size) : 0);
		draw_list_item(y, row_h - 6, i == sel, title, pct);
		y += row_h;
	}
}

void screens_reader_metrics(struct reader_metrics *m)
{
	const struct font *f = &font_read_m;

	m->width_px = CW;
	m->lines = (uint8_t)((FOOTER_Y - 30) / f->height);
	m->width = gfx_reader_width;
	m->width_ctx = (void *)f;
}

static void draw_reader(void)
{
	static char line[160];
	const struct font *f = &font_read_m;
	struct reader *r = &g_reading.r;
	int y = 24;

	if (!g_reading.open) {
		gfx_text_center(&font_ui, GFX_H / 2, "This book could not be opened");
		return;
	}
	for (uint8_t i = 0; i < r->page.nlines; i++) {
		size_t n = MIN(r->page.line[i].len, sizeof(line));

		n = reader_src_copy(&r->src, r->page.line[i].start, line, n);
		gfx_text(f, M, y, line, n);
		y += f->height;
	}
	char foot[32];
	uint32_t total = g_reading.info.text_len ? g_reading.info.text_len : 1;

	snprintf(foot, sizeof(foot), "%u%%", (unsigned)((uint64_t)r->page.end * 100 / total));
	gfx_text_right(&font_ui, GFX_W - M, FOOTER_Y + 6, foot);
}

/* ---- Settings and system screens ----------------------------------------------------------- */

static void draw_settings(void)
{
	static const char *const strength[] = {"Off", "Low", "Medium", "High"};
	static const char *const refresh[] = {"Quality", "Balanced", "Fast"};
	const struct op_settings *s = &g_app.settings;
	char right[40];
	const int row_h = 62;
	int y = HEADER_H + 14;

	header("Settings");
	for (int i = 0; i < SET_ITEM_COUNT; i++) {
		const char *label = "";

		right[0] = 0;
		switch (i) {
		case SET_ALERT_STRENGTH:
			label = "Alert strength";
			snprintf(right, sizeof(right), "%s", strength[s->alert_strength & 3]);
			break;
		case SET_QUIET_HOURS:
			label = "Quiet hours";
			if (s->quiet_on) {
				snprintf(right, sizeof(right), "%02u:%02u-%02u:%02u", s->quiet_start / 60,
					 s->quiet_start % 60, s->quiet_end / 60, s->quiet_end % 60);
			} else {
				snprintf(right, sizeof(right), "Off");
			}
			break;
		case SET_REFRESH_MODE:
			label = "Refresh";
			snprintf(right, sizeof(right), "%s", refresh[MIN(s->refresh_mode, 2)]);
			break;
		case SET_CAL_AWAY:
			label = "Calendar, phone away";
			snprintf(right, sizeof(right), "%s",
				 g_app.privacy_cfg.show_calendar_away ? "Shown" : "Hidden");
			break;
		case SET_DEVICE_INFO: label = "Device info"; break;
		case SET_PAIR_NEW: label = "Pair a new phone"; break;
		case SET_ERASE: label = "Erase and reset"; break;
		default: break;
		}
		draw_list_item(y, row_h - 6, g_app.nav.sel[UI_SETTINGS] == i, label,
			       right[0] ? right : NULL);
		y += row_h;
	}
	footer("Tap: change   Hold: back");
}

static void draw_confirm(void)
{
	bool erase = g_app.nav.confirm_what == SET_ERASE;

	header(erase ? "Erase and reset" : "Pair a new phone");
	gfx_wrap(&font_ui_bold, M, HEADER_H + 80, CW,
		 erase ? "Erase calendar, weather, settings, app list and books, and forget the "
			 "phone?"
		       : "Forget this phone and erase calendar, weather, settings and app list? "
			 "Books stay.",
		 200, 0, 6);
	footer("Tap: confirm   Hold: cancel");
}

static void draw_info(void)
{
	char buf[64];
	int y = HEADER_H + 30;
	uint32_t fk, tk;

	header("Device info");
#define LINE(...)                                              \
	do {                                                   \
		snprintf(buf, sizeof(buf), __VA_ARGS__);       \
		gfx_text(&font_ui, M, y, buf, strlen(buf));    \
		y += font_ui.height + 8;                       \
	} while (0)
	LINE("Device ID  %s", g_app.device_id);
	LINE("Firmware  %s", CONFIG_ONEPAGE_FW_VERSION);
	LINE("Hardware  %d", CONFIG_ONEPAGE_HW_REV);
	if (g_app.battery.gauge_ok) {
		LINE("Battery  %u%%  %u mV", g_app.battery.percent, g_app.battery.millivolts);
	} else {
		LINE("Battery  unknown");
	}
	books_storage(&fk, &tk);
	LINE("Books  %u of %u MB free", fk / 1024, tk / 1024);
	LINE("Phone  %s", g_app.bonded ? (g_app.connected ? "paired, connected" : "paired")
				       : "none");
	LINE("Calendar  %u events", cal_count(&g_app.cal));
	if (g_app.last_error) {
		LINE("Last error  0x%04x", g_app.last_error);
	}
#undef LINE
	footer("Hold: back");
}

static void draw_power_menu(void)
{
	static const char *const items[PWR_ITEM_COUNT] = {"Power off", "Restart", "Cancel"};
	int y = HEADER_H + 60;

	header("Power");
	for (int i = 0; i < PWR_ITEM_COUNT; i++) {
		draw_list_item(y, 64, g_app.nav.sel[UI_POWER_MENU] == i, items[i], NULL);
		y += 70;
	}
}

static void draw_charging(void)
{
	char buf[32];

	if (g_app.battery.gauge_ok) {
		snprintf(buf, sizeof(buf), "%u%%", g_app.battery.percent);
		gfx_text_center(&font_clock, GFX_H / 2 - 120, buf);
	}
	gfx_text_center(&font_title, GFX_H / 2 + 40,
			g_app.battery.charging == OP_CHG_FULL ? "Charged" : "Charging");
	if (g_app.battery.charging == OP_CHG_FAULT) {
		gfx_text_center(&font_ui, GFX_H / 2 + 100, "Charger fault: unplug and try again");
	}
}

static void draw_pairing(void)
{
	char buf[48];

	header("Pairing");
	const char *how = g_app.pairing_window
		? "Open the OnePage app on your iPhone. It finds this device."
		: "Hold your iPhone against the back of this device, or choose Settings, "
		  "Pair a new phone.";

	gfx_wrap(&font_ui_bold, M, HEADER_H + 60, CW, how, strlen(how), 0, 5);
	gfx_wrap(&font_ui, M, HEADER_H + 260, CW,
		 "Accept both iPhone prompts: pairing, then sharing notifications.", 200, 0, 4);
	snprintf(buf, sizeof(buf), "Device %s", g_app.device_id);
	gfx_text_center(&font_title, GFX_H - 220, buf);
	footer(g_app.pairing_window ? "Pairing open for 2 minutes" : "Pairing closed");
}

/* ---- entry points ---------------------------------------------------------------------- */

void screens_draw(const struct ui_ctx *ctx)
{
	gfx_clear();
	switch (g_app.nav.screen) {
	case UI_HOME: draw_home(ctx); break;
	case UI_NOTIF_LIST: draw_notif_list(); break;
	case UI_NOTIF_DETAIL: draw_notif_detail(); break;
	case UI_CALENDAR: draw_calendar(); break;
	case UI_CAL_DETAIL: draw_cal_detail(); break;
	case UI_MUSIC: draw_music(); break;
	case UI_LIBRARY: draw_library(); break;
	case UI_READER: draw_reader(); break;
	case UI_SETTINGS: draw_settings(); break;
	case UI_CONFIRM: draw_confirm(); break;
	case UI_INFO: draw_info(); break;
	case UI_POWER_MENU: draw_power_menu(); break;
	case UI_CHARGING: draw_charging(); break;
	case UI_PAIRING: draw_pairing(); break;
	default: draw_home(ctx); break;
	}
}

uint16_t screens_detail_pages(const struct ui_nav *nav)
{
	if (nav->screen != UI_NOTIF_DETAIL) {
		return 1;
	}
	const struct notif *x = notif_list_newest(&g_app.notifs, (uint8_t)nav->detail);

	if (!x || x->title_only) {
		return 1;
	}
	int lines = notif_detail_lines(x);

	return (uint16_t)MAX(1, (lines + DETAIL_LINES - 1) / DETAIL_LINES);
}

void screens_draw_message(const char *title, const char *body)
{
	gfx_clear();
	gfx_text_center(&font_title, GFX_H / 2 - 80, title);
	gfx_wrap(&font_ui, M, GFX_H / 2, CW, body, strlen(body), 0, 6);
}

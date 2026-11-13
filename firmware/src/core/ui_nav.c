#include "ui_nav.h"

#include <string.h>

void ui_nav_init(struct ui_nav *n)
{
	memset(n, 0, sizeof(*n));
	n->screen = UI_HOME;
}

uint8_t ui_home_items(const struct ui_ctx *c, enum ui_home_item items[HOME_ITEM_COUNT])
{
	uint8_t k = 0;

	if (!c->locked) {
		items[k++] = HOME_NOTIFICATIONS;
	}
	if (c->cal_visible) {
		items[k++] = HOME_CALENDAR;
	}
	if (!c->locked) {
		items[k++] = HOME_MUSIC;
	}
	items[k++] = HOME_LIBRARY;
	items[k++] = HOME_SETTINGS;
	return k;
}

static uint16_t list_len(const struct ui_nav *n, const struct ui_ctx *c, enum ui_screen s)
{
	enum ui_home_item items[HOME_ITEM_COUNT];

	(void)n;
	switch (s) {
	case UI_HOME: return ui_home_items(c, items);
	case UI_NOTIF_LIST: return c->notif_count;
	case UI_CALENDAR: return c->cal_count;
	case UI_LIBRARY: return c->book_count;
	case UI_SETTINGS: return SET_ITEM_COUNT;
	case UI_POWER_MENU: return PWR_ITEM_COUNT;
	default: return 0;
	}
}

static bool allowed(const struct ui_ctx *c, enum ui_screen s)
{
	switch (s) {
	case UI_NOTIF_LIST:
	case UI_NOTIF_DETAIL:
	case UI_MUSIC:
		return !c->locked;
	case UI_CALENDAR:
	case UI_CAL_DETAIL:
		return c->cal_visible;
	default:
		return true;
	}
}

static void clamp_sel(struct ui_nav *n, const struct ui_ctx *c, enum ui_screen s)
{
	uint16_t len = list_len(n, c, s);

	if (len == 0) {
		n->sel[s] = 0;
	} else if (n->sel[s] >= len) {
		n->sel[s] = len - 1;
	}
}

void ui_nav_show(struct ui_nav *n, enum ui_screen s)
{
	n->screen = s;
}

static struct ui_result go(struct ui_nav *n, enum ui_screen s, enum ui_act act, uint16_t idx)
{
	struct ui_result r = {.redraw = true, .act = act, .index = idx};

	n->screen = s;
	return r;
}

static struct ui_result nothing(void)
{
	struct ui_result r = {false, UI_ACT_NONE, 0};

	return r;
}

static struct ui_result move_sel(struct ui_nav *n, const struct ui_ctx *c, int d)
{
	uint16_t len = list_len(n, c, n->screen);
	uint16_t *sel = &n->sel[n->screen];

	if (len == 0) {
		return nothing();
	}
	if (d > 0 && *sel + 1 < len) {
		(*sel)++;
	} else if (d < 0 && *sel > 0) {
		(*sel)--;
	} else {
		return nothing();
	}
	struct ui_result r = {true, UI_ACT_NONE, 0};

	return r;
}

static enum ui_screen parent(enum ui_screen s)
{
	switch (s) {
	case UI_NOTIF_DETAIL: return UI_NOTIF_LIST;
	case UI_CAL_DETAIL: return UI_CALENDAR;
	case UI_READER: return UI_LIBRARY;
	case UI_CONFIRM:
	case UI_INFO: return UI_SETTINGS;
	default: return UI_HOME;
	}
}

static struct ui_result home_tap(struct ui_nav *n, const struct ui_ctx *c)
{
	enum ui_home_item items[HOME_ITEM_COUNT];
	uint8_t k = ui_home_items(c, items);

	clamp_sel(n, c, UI_HOME);
	if (k == 0) {
		return nothing();
	}
	switch (items[n->sel[UI_HOME]]) {
	case HOME_NOTIFICATIONS:
		n->sel[UI_NOTIF_LIST] = 0;
		return go(n, UI_NOTIF_LIST, UI_ACT_NONE, 0);
	case HOME_CALENDAR:
		n->sel[UI_CALENDAR] = 0;
		return go(n, UI_CALENDAR, UI_ACT_NONE, 0);
	case HOME_MUSIC:
		return go(n, UI_MUSIC, UI_ACT_NONE, 0);
	case HOME_LIBRARY:
		clamp_sel(n, c, UI_LIBRARY);
		return go(n, UI_LIBRARY, UI_ACT_NONE, 0);
	case HOME_SETTINGS:
		return go(n, UI_SETTINGS, UI_ACT_NONE, 0);
	default:
		return nothing();
	}
}

static struct ui_result tap(struct ui_nav *n, const struct ui_ctx *c)
{
	uint16_t sel = n->sel[n->screen];

	switch (n->screen) {
	case UI_HOME:
		return home_tap(n, c);
	case UI_NOTIF_LIST:
		if (c->notif_count == 0) {
			return nothing();
		}
		n->detail = sel;
		n->detail_page = 0;
		return go(n, UI_NOTIF_DETAIL, UI_ACT_NOTIF_OPENED, sel);
	case UI_NOTIF_DETAIL: {
		uint16_t idx = n->detail;
		struct ui_result r = go(n, UI_NOTIF_LIST, UI_ACT_NOTIF_NEGATIVE, idx);

		return r;
	}
	case UI_CALENDAR:
		if (c->cal_count == 0) {
			return nothing();
		}
		n->detail = sel;
		n->detail_page = 0;
		return go(n, UI_CAL_DETAIL, UI_ACT_NONE, 0);
	case UI_CAL_DETAIL:
		return go(n, UI_CALENDAR, UI_ACT_NONE, 0);
	case UI_MUSIC: {
		struct ui_result r = {true, UI_ACT_MUSIC_TOGGLE, 0};

		return r;
	}
	case UI_LIBRARY:
		if (c->book_count == 0) {
			return nothing();
		}
		return go(n, UI_READER, UI_ACT_BOOK_OPEN, sel);
	case UI_SETTINGS:
		if (sel == SET_DEVICE_INFO) {
			return go(n, UI_INFO, UI_ACT_NONE, 0);
		}
		if (sel == SET_PAIR_NEW || sel == SET_ERASE) {
			n->confirm_what = (uint8_t)sel;
			return go(n, UI_CONFIRM, UI_ACT_NONE, 0);
		}
		{
			struct ui_result r = {true, UI_ACT_SETTING, sel};

			return r;
		}
	case UI_CONFIRM:
		return go(n, UI_HOME,
			  n->confirm_what == SET_ERASE ? UI_ACT_ERASE_ALL : UI_ACT_PAIR_NEW, 0);
	case UI_INFO:
		return go(n, UI_SETTINGS, UI_ACT_NONE, 0);
	case UI_POWER_MENU:
		if (sel == PWR_OFF) {
			return go(n, UI_HOME, UI_ACT_POWER_OFF, 0);
		}
		if (sel == PWR_RESTART) {
			return go(n, UI_HOME, UI_ACT_RESTART, 0);
		}
		return go(n, n->before_power, UI_ACT_NONE, 0);
	case UI_CHARGING:
	case UI_PAIRING:
		return go(n, UI_HOME, UI_ACT_NONE, 0);
	case UI_READER:
	default:
		return nothing();
	}
}

static struct ui_result swipe(struct ui_nav *n, const struct ui_ctx *c, int d)
{
	switch (n->screen) {
	case UI_READER: {
		struct ui_result r = {false, d > 0 ? UI_ACT_PAGE_NEXT : UI_ACT_PAGE_PREV, 0};

		return r;
	}
	case UI_MUSIC: {
		struct ui_result r = {true, d > 0 ? UI_ACT_MUSIC_NEXT : UI_ACT_MUSIC_PREV, 0};

		return r;
	}
	case UI_NOTIF_DETAIL:
	case UI_CAL_DETAIL:
		if (d > 0 && n->detail_page + 1 < c->detail_pages) {
			n->detail_page++;
		} else if (d < 0 && n->detail_page > 0) {
			n->detail_page--;
		} else {
			return nothing();
		}
		{
			struct ui_result r = {true, UI_ACT_NONE, 0};

			return r;
		}
	case UI_CHARGING:
	case UI_PAIRING:
		return go(n, UI_HOME, UI_ACT_NONE, 0);
	default:
		return move_sel(n, c, d);
	}
}

struct ui_result ui_nav_input(struct ui_nav *n, const struct ui_ctx *c, enum ui_input in)
{
	if (!allowed(c, n->screen)) {
		n->screen = UI_HOME;
	}
	clamp_sel(n, c, n->screen);

	switch (in) {
	case UI_IN_FWD:
		return swipe(n, c, +1);
	case UI_IN_BACK:
		return swipe(n, c, -1);
	case UI_IN_TAP:
		return tap(n, c);
	case UI_IN_LONG:
		if (n->screen == UI_HOME) {
			return nothing();
		}
		if (n->screen == UI_POWER_MENU) {
			return go(n, n->before_power, UI_ACT_NONE, 0);
		}
		return go(n, parent(n->screen),
			  n->screen == UI_READER ? UI_ACT_BOOK_CLOSE : UI_ACT_NONE, 0);
	case UI_IN_BTN_SHORT:
		if (n->screen == UI_HOME) {
			/* Already Home: the press wakes the screen and asks for fresh data. */
			return go(n, UI_HOME, UI_ACT_REQUEST_REFRESH, 0);
		}
		{
			enum ui_act a = n->screen == UI_READER ? UI_ACT_BOOK_CLOSE : UI_ACT_NONE;

			n->sel[UI_HOME] = 0;
			return go(n, UI_HOME, a, 0);
		}
	case UI_IN_BTN_HOLD:
		if (n->screen != UI_POWER_MENU) {
			n->before_power = n->screen == UI_READER ? UI_LIBRARY : n->screen;
			n->sel[UI_POWER_MENU] = PWR_CANCEL;
		}
		return go(n, UI_POWER_MENU,
			  n->screen == UI_READER ? UI_ACT_BOOK_CLOSE : UI_ACT_NONE, 0);
	default:
		return nothing();
	}
}

bool ui_nav_lock(struct ui_nav *n, const struct ui_ctx *c)
{
	switch (n->screen) {
	case UI_LIBRARY:
	case UI_READER:
	case UI_SETTINGS:
	case UI_INFO:
	case UI_CONFIRM:
	case UI_CHARGING:
		return false;
	case UI_CALENDAR:
	case UI_CAL_DETAIL:
		if (c->cal_visible) {
			return false;
		}
		break;
	default:
		break;
	}
	n->screen = UI_HOME;
	n->sel[UI_HOME] = 0;
	n->sel[UI_NOTIF_LIST] = 0;
	return true;
}

bool ui_nav_refresh(struct ui_nav *n, const struct ui_ctx *c)
{
	bool changed = false;

	if (!allowed(c, n->screen)) {
		n->screen = UI_HOME;
		changed = true;
	}
	if (n->screen == UI_NOTIF_DETAIL && n->detail >= c->notif_count) {
		n->screen = UI_NOTIF_LIST;
		changed = true;
	}
	if (n->screen == UI_CAL_DETAIL && n->detail >= c->cal_count) {
		n->screen = UI_CALENDAR;
		changed = true;
	}
	for (int s = 0; s < UI_SCREEN_COUNT; s++) {
		uint16_t before = n->sel[s];

		clamp_sel(n, c, (enum ui_screen)s);
		if (before != n->sel[s] && s == (int)n->screen) {
			changed = true;
		}
	}
	return changed;
}

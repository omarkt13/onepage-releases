/*
 * OnePage Mini - screen navigation (firmware spec section 5).
 * Pure state machine: inputs in, a redraw flag and an action out.
 */
#ifndef OP_UI_NAV_H
#define OP_UI_NAV_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ui_screen {
	UI_HOME = 0,
	UI_NOTIF_LIST,
	UI_NOTIF_DETAIL,
	UI_CALENDAR,
	UI_CAL_DETAIL,
	UI_MUSIC,
	UI_LIBRARY,
	UI_READER,
	UI_SETTINGS,
	UI_CONFIRM,
	UI_INFO,
	UI_POWER_MENU,
	UI_CHARGING,
	UI_PAIRING,
	UI_SCREEN_COUNT
};

enum ui_home_item {
	HOME_NOTIFICATIONS = 0,
	HOME_CALENDAR,
	HOME_MUSIC,
	HOME_LIBRARY,
	HOME_SETTINGS,
	HOME_ITEM_COUNT
};

enum ui_setting_item {
	SET_ALERT_STRENGTH = 0,
	SET_QUIET_HOURS,
	SET_REFRESH_MODE,
	SET_CAL_AWAY,
	SET_DEVICE_INFO,
	SET_PAIR_NEW,
	SET_ERASE,
	SET_ITEM_COUNT
};

enum ui_power_item { PWR_OFF = 0, PWR_RESTART, PWR_CANCEL, PWR_ITEM_COUNT };

enum ui_input {
	UI_IN_FWD = 0,   /* swipe forward */
	UI_IN_BACK,      /* swipe back */
	UI_IN_TAP,
	UI_IN_LONG,      /* long press on the strip: Back */
	UI_IN_BTN_SHORT, /* power button: wake or Home */
	UI_IN_BTN_HOLD,  /* power button 3 s: power menu */
};

enum ui_act {
	UI_ACT_NONE = 0,
	UI_ACT_NOTIF_OPENED,   /* index: newest-first position; mark read */
	UI_ACT_NOTIF_NEGATIVE, /* dismiss on phone, or decline a call */
	UI_ACT_MUSIC_TOGGLE,
	UI_ACT_MUSIC_NEXT,
	UI_ACT_MUSIC_PREV,
	UI_ACT_BOOK_OPEN,      /* index: library position */
	UI_ACT_PAGE_NEXT,
	UI_ACT_PAGE_PREV,
	UI_ACT_BOOK_CLOSE,     /* save position; full refresh */
	UI_ACT_SETTING,        /* index: ui_setting_item to toggle or cycle */
	UI_ACT_PAIR_NEW,       /* confirmed */
	UI_ACT_ERASE_ALL,      /* confirmed */
	UI_ACT_POWER_OFF,
	UI_ACT_RESTART,
	UI_ACT_REQUEST_REFRESH,/* ask the app for fresh weather and calendar */
};

/* Facts the state machine needs, supplied by the caller at each input. */
struct ui_ctx {
	uint8_t notif_count;
	uint16_t cal_count;
	uint16_t book_count;
	bool locked;
	bool cal_visible;
	uint16_t detail_pages; /* pages of the open notification or event */
};

struct ui_nav {
	enum ui_screen screen;
	enum ui_screen before_power; /* screen to return to from the power menu */
	uint16_t sel[UI_SCREEN_COUNT];
	uint16_t detail;      /* index of the open notification or event */
	uint16_t detail_page;
	uint8_t confirm_what; /* SET_PAIR_NEW or SET_ERASE */
};

struct ui_result {
	bool redraw;
	enum ui_act act;
	uint16_t index;
};

void ui_nav_init(struct ui_nav *n);
/* Items shown on Home for this context. Returns the count. */
uint8_t ui_home_items(const struct ui_ctx *c, enum ui_home_item items[HOME_ITEM_COUNT]);
struct ui_result ui_nav_input(struct ui_nav *n, const struct ui_ctx *c, enum ui_input in);
/* Phone away for 2 minutes. Books and Settings stay; private screens close. */
bool ui_nav_lock(struct ui_nav *n, const struct ui_ctx *c);
/* Data changed (for example a notification removed): keep indexes valid. */
bool ui_nav_refresh(struct ui_nav *n, const struct ui_ctx *c);
void ui_nav_show(struct ui_nav *n, enum ui_screen s);

#ifdef __cplusplus
}
#endif

#endif /* OP_UI_NAV_H */

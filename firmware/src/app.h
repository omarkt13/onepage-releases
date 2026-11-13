/*
 * OnePage Mini - shared application state and the event queue.
 *
 * One application thread (main.c) owns the UI and all decisions. Bluetooth
 * callbacks, drivers and timers post events to it. Shared state is guarded
 * by app_lock(); hold it only for short copies, never across a display
 * refresh or a flash write.
 */
#ifndef OP_APP_H
#define OP_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "core/alerts.h"
#include "core/app_filter.h"
#include "core/calendar.h"
#include "core/notif_list.h"
#include "core/op_proto.h"
#include "core/privacy.h"
#include "core/ui_nav.h"

enum app_evt_type {
	EVT_INPUT = 1,         /* a = enum ui_input */
	EVT_CONNECTED,
	EVT_DISCONNECTED,
	EVT_LINK_READY,        /* encrypted and iOS services discovered */
	EVT_NOTIF_ADDED,       /* b = uid */
	EVT_NOTIF_REMOVED,     /* b = uid */
	EVT_MUSIC,             /* track or play state changed */
	EVT_TIME_SET,
	EVT_WEATHER,
	EVT_CALENDAR,          /* calendar replaced or cleared; persist it */
	EVT_SETTINGS,          /* settings written; persist them */
	EVT_FILTER,            /* app filter changed; persist it */
	EVT_PRIVACY,           /* privacy settings written; persist them */
	EVT_ERASE_ALL,         /* ERAS from the app */
	EVT_BATTERY,
	EVT_CHARGER,
	EVT_NFC_FIELD,
	EVT_BOOKS,             /* library changed */
	EVT_PAIRING_DONE,
	EVT_PAIRING_WINDOW_END,
	EVT_TOUCH,             /* a touch sample is ready */
	EVT_TICK,
};

struct app_evt {
	uint8_t type;
	uint8_t a;
	uint32_t b;
};

void app_post(uint8_t type, uint8_t a, uint32_t b);

struct music_state {
	bool valid;
	bool playing;
	char title[64];  /* Windows-1252 */
	char artist[48];
	char album[48];
};

struct battery_state {
	bool gauge_ok;
	uint8_t percent;
	uint8_t charging; /* enum op_charging */
	uint16_t millivolts;
};

struct app_state {
	struct op_settings settings;
	struct op_privacy privacy_cfg;
	struct privacy privacy;
	struct op_weather weather;
	struct app_filter filter;
	struct notif_list notifs;
	struct cal_store cal;
	struct music_state music;
	struct battery_state battery;
	struct ui_nav nav;
	uint16_t last_error;
	bool connected;
	bool link_ready;
	bool bonded;
	bool pairing_window;
	char device_id[9];
};

extern struct app_state g_app;

void app_lock(void);
void app_unlock(void);

/* Records an error for the Status characteristic. */
void app_set_error(uint16_t code);

/* Builds the Status characteristic value. */
size_t app_status_encode(uint8_t *buf, size_t cap);

/* Erases the owner's data (calendar, weather, settings, filter). */
void app_erase_personal(void);

#endif /* OP_APP_H */

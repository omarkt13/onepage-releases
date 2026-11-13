/*
 * OnePage Mini - persistent settings in the chip's internal RRAM (Zephyr
 * settings on ZMS). Settings, privacy, app filter, the calendar and reading
 * positions live here. Never the external flash (spec section 7), and never
 * notification text.
 */
#ifndef OP_STORE_H
#define OP_STORE_H

#include <stdint.h>

/* Loads everything, including Bluetooth bonds, into g_app. */
int store_init(void);
void store_save_settings(void);
void store_save_privacy(void);
void store_save_filter(void);
/* Writes the current calendar to the inactive bank, then switches banks. */
void store_save_calendar(void);
/* Deletes calendar, settings, privacy and filter. */
void store_erase_personal(void);

/* Time without the phone in minutes, so the 7-day rule survives a restart. */
void store_save_away(uint32_t minutes);

uint32_t store_get_position(uint32_t book_id);
void store_set_position(uint32_t book_id, uint32_t position);
void store_delete_position(uint32_t book_id);

#endif

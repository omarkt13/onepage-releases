/* OnePage Mini - screen drawing (firmware spec section 5). */
#ifndef OP_SCREENS_H
#define OP_SCREENS_H

#include <stdint.h>

#include "core/reader.h"
#include "core/ui_nav.h"

/* Reading session shown by the Reader screen. */
struct reading {
	bool open;
	uint32_t book_id;
	uint32_t size;
	struct opb_info info;
	struct reader r;
};

extern struct reading g_reading;

/* Draws the current screen into gfx_fb. Call with app_lock() held. */
void screens_draw(const struct ui_ctx *ctx);
/* Pages of the open notification or event (for struct ui_ctx). Lock held. */
uint16_t screens_detail_pages(const struct ui_nav *nav);
/* A full-screen message, for example before switching off. */
void screens_draw_message(const char *title, const char *body);
/* Reader layout for the current reading size. */
void screens_reader_metrics(struct reader_metrics *m);

#endif

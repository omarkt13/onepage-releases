/* OnePage Mini - 1-bit frame buffer drawing. Text is Windows-1252. */
#ifndef OP_GFX_H
#define OP_GFX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/epd.h"
#include "ui/fonts.h"

#define GFX_W EPD_WIDTH
#define GFX_H EPD_HEIGHT

extern uint8_t gfx_fb[EPD_FB_SIZE];

void gfx_clear(void);
void gfx_fill(int x, int y, int w, int h, bool black);
void gfx_invert(int x, int y, int w, int h);
void gfx_hline(int x, int y, int w, int thickness);

uint8_t gfx_char_width(const struct font *f, uint8_t ch);
int gfx_text_width(const struct font *f, const char *s, size_t len);
/* Draws text with its top at y. Returns the x after it. */
int gfx_text(const struct font *f, int x, int y, const char *s, size_t len);
/* Draws a terminated string cut with an ellipsis to fit max_w. */
int gfx_text_fit(const struct font *f, int x, int y, const char *s, int max_w);
/* Right-aligned variant: text ends at x_right. */
void gfx_text_right(const struct font *f, int x_right, int y, const char *s);
void gfx_text_center(const struct font *f, int y, const char *s);
/*
 * Word-wraps text into width w. Skips `skip` lines, draws up to max_lines.
 * Returns the total number of lines the text needs.
 */
int gfx_wrap(const struct font *f, int x, int y, int w, const char *s, size_t len, int skip,
	     int max_lines);

/*
 * Rows that differ from the frame last marked shown. Returns false if
 * nothing changed.
 */
bool gfx_changed_rows(uint16_t *y0, uint16_t *y1);
void gfx_mark_shown(void);
void gfx_forget_shown(void);

/* reader_width_fn adapter: ctx is a const struct font *. */
uint8_t gfx_reader_width(void *ctx, uint8_t ch);

#endif

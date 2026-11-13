/* OnePage Mini - e-paper display (Sunshine SDE0368NQW-52, UC81xx controller). */
#ifndef OP_EPD_H
#define OP_EPD_H

#include <stdbool.h>
#include <stdint.h>

#define EPD_WIDTH 528
#define EPD_HEIGHT 792
#define EPD_STRIDE (EPD_WIDTH / 8)
#define EPD_FB_SIZE (EPD_STRIDE * EPD_HEIGHT)

enum epd_mode {
	EPD_FULL,    /* flashing refresh, clears ghosting */
	EPD_FAST,    /* faster whole-screen refresh, some ghosting */
	EPD_PARTIAL, /* window refresh, no flash; page turns and clock */
};

int epd_init(void);
bool epd_present(void);
/*
 * Shows a frame. fb: 1 bit per pixel, 1 = black, MSB is the leftmost pixel.
 * For EPD_PARTIAL only rows y0..y1 (inclusive) are sent. The driver turns a
 * partial request into a full one when the controller lost its RAM or the
 * ghosting count says so. Blocks until the panel is done.
 */
int epd_show(const uint8_t *fb, enum epd_mode mode, uint16_t y0, uint16_t y1);
/* Refreshes since the last full refresh. */
uint8_t epd_partial_count(void);
/* Cuts the panel supply now (System OFF, long idle). */
void epd_power_off(void);

#endif

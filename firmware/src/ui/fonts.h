/* OnePage Mini - bitmap fonts for Windows-1252 (see tools/gen_fonts.py). */
#ifndef OP_FONTS_H
#define OP_FONTS_H

#include <stdint.h>

struct font_glyph {
	uint32_t offset; /* into bits */
	uint8_t advance;
	uint8_t w;
	uint8_t h;
	int8_t x; /* left bearing */
	int8_t y; /* rows from the top of the line */
};

struct font {
	uint8_t height;
	uint8_t ascent;
	uint8_t first;
	uint8_t last;
	const struct font_glyph *glyphs;
	const uint8_t *bits; /* rows, MSB first, (w + 7) / 8 bytes each */
};

extern const struct font font_ui;
extern const struct font font_ui_bold;
extern const struct font font_title;
extern const struct font font_clock;
extern const struct font font_read_s;
extern const struct font font_read_m;
extern const struct font font_read_l;

#endif /* OP_FONTS_H */

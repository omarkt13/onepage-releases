#include "ui/gfx.h"

#include <string.h>

#include <zephyr/sys/util.h>

#include "core/op_util.h"

uint8_t gfx_fb[EPD_FB_SIZE];
static uint16_t row_hash[GFX_H];
static bool shown_valid;

void gfx_clear(void)
{
	memset(gfx_fb, 0, sizeof(gfx_fb));
}

static inline void px(int x, int y, bool black)
{
	if ((unsigned)x >= GFX_W || (unsigned)y >= GFX_H) {
		return;
	}
	uint8_t *b = &gfx_fb[y * EPD_STRIDE + (x >> 3)];
	uint8_t m = 0x80 >> (x & 7);

	*b = black ? (*b | m) : (*b & ~m);
}

void gfx_fill(int x, int y, int w, int h, bool black)
{
	for (int j = y; j < y + h; j++) {
		for (int i = x; i < x + w; i++) {
			px(i, j, black);
		}
	}
}

void gfx_invert(int x, int y, int w, int h)
{
	for (int j = MAX(y, 0); j < MIN(y + h, GFX_H); j++) {
		for (int i = MAX(x, 0); i < MIN(x + w, GFX_W); i++) {
			gfx_fb[j * EPD_STRIDE + (i >> 3)] ^= 0x80 >> (i & 7);
		}
	}
}

void gfx_hline(int x, int y, int w, int t)
{
	gfx_fill(x, y, w, t, true);
}

static const struct font_glyph *glyph(const struct font *f, uint8_t ch)
{
	if (ch < f->first || ch > f->last) {
		ch = '?';
	}
	const struct font_glyph *g = &f->glyphs[ch - f->first];

	if (g->advance == 0 && ch != '?') {
		return glyph(f, '?');
	}
	return g;
}

uint8_t gfx_char_width(const struct font *f, uint8_t ch)
{
	return glyph(f, ch)->advance;
}

uint8_t gfx_reader_width(void *ctx, uint8_t ch)
{
	return gfx_char_width((const struct font *)ctx, ch);
}

int gfx_text_width(const struct font *f, const char *s, size_t len)
{
	int w = 0;

	for (size_t i = 0; i < len && s[i]; i++) {
		w += gfx_char_width(f, (uint8_t)s[i]);
	}
	return w;
}

/* Inverted pixels inside a highlight are handled by XOR-drawing text. */
static void draw_glyph(const struct font *f, const struct font_glyph *g, int x, int y)
{
	const uint8_t *bits = f->bits + g->offset;
	int rb = (g->w + 7) / 8;

	for (int r = 0; r < g->h; r++) {
		for (int c = 0; c < g->w; c++) {
			if (bits[r * rb + (c >> 3)] & (0x80 >> (c & 7))) {
				int X = x + g->x + c, Y = y + g->y + r;

				if ((unsigned)X < GFX_W && (unsigned)Y < GFX_H) {
					gfx_fb[Y * EPD_STRIDE + (X >> 3)] ^= 0x80 >> (X & 7);
				}
			}
		}
	}
}

int gfx_text(const struct font *f, int x, int y, const char *s, size_t len)
{
	for (size_t i = 0; i < len && s[i]; i++) {
		const struct font_glyph *g = glyph(f, (uint8_t)s[i]);

		draw_glyph(f, g, x, y);
		x += g->advance;
	}
	return x;
}

int gfx_text_fit(const struct font *f, int x, int y, const char *s, int max_w)
{
	size_t len = strlen(s);

	if (gfx_text_width(f, s, len) <= max_w) {
		return gfx_text(f, x, y, s, len);
	}
	const char ell = '\x85'; /* Windows-1252 ellipsis */
	int ew = gfx_char_width(f, (uint8_t)ell);
	int w = 0;
	size_t n = 0;

	while (n < len && w + gfx_char_width(f, (uint8_t)s[n]) + ew <= max_w) {
		w += gfx_char_width(f, (uint8_t)s[n]);
		n++;
	}
	while (n > 0 && s[n - 1] == ' ') {
		n--;
	}
	x = gfx_text(f, x, y, s, n);
	return gfx_text(f, x, y, &ell, 1);
}

void gfx_text_right(const struct font *f, int x_right, int y, const char *s)
{
	gfx_text(f, x_right - gfx_text_width(f, s, strlen(s)), y, s, strlen(s));
}

void gfx_text_center(const struct font *f, int y, const char *s)
{
	int w = gfx_text_width(f, s, strlen(s));

	gfx_text(f, (GFX_W - w) / 2, y, s, strlen(s));
}

int gfx_wrap(const struct font *f, int x, int y, int w, const char *s, size_t len, int skip,
	     int max_lines)
{
	size_t i = 0;
	int line = 0;

	while (i < len && s[i]) {
		size_t start = i, last_space = (size_t)-1, end;
		int lw = 0;

		while (i < len && s[i] && s[i] != '\n') {
			int cw = gfx_char_width(f, (uint8_t)s[i]);

			if (lw + cw > w && i > start) {
				break;
			}
			if (s[i] == ' ') {
				last_space = i;
			}
			lw += cw;
			i++;
		}
		if (i < len && s[i] && s[i] != '\n' && last_space != (size_t)-1) {
			end = last_space;
			i = last_space + 1;
		} else {
			end = i;
			if (i < len && s[i] == '\n') {
				i++;
			}
		}
		if (line >= skip && line - skip < max_lines) {
			gfx_text(f, x, y + (line - skip) * f->height, s + start, end - start);
		}
		line++;
	}
	return line;
}

bool gfx_changed_rows(uint16_t *y0, uint16_t *y1)
{
	int first = -1, last = -1;

	for (int y = 0; y < GFX_H; y++) {
		uint16_t h = op_crc16(0xFFFF, &gfx_fb[y * EPD_STRIDE], EPD_STRIDE);

		if (!shown_valid || h != row_hash[y]) {
			if (first < 0) {
				first = y;
			}
			last = y;
		}
	}
	if (first < 0) {
		return false;
	}
	*y0 = (uint16_t)first;
	*y1 = (uint16_t)last;
	return true;
}

void gfx_mark_shown(void)
{
	for (int y = 0; y < GFX_H; y++) {
		row_hash[y] = op_crc16(0xFFFF, &gfx_fb[y * EPD_STRIDE], EPD_STRIDE);
	}
	shown_valid = true;
}

void gfx_forget_shown(void)
{
	shown_valid = false;
}

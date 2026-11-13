#include "text.h"

/* Windows-1252 bytes 0x80..0x9F and their Unicode code points. */
static const uint16_t cp1252_high[32] = {
	0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000,
	0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178,
};

size_t op_utf8_decode(const uint8_t *s, size_t len, uint32_t *cp)
{
	uint8_t b = s[0];
	size_t n;
	uint32_t c;

	if (b < 0x80) {
		*cp = b;
		return 1;
	} else if ((b & 0xE0) == 0xC0) {
		n = 2;
		c = b & 0x1F;
	} else if ((b & 0xF0) == 0xE0) {
		n = 3;
		c = b & 0x0F;
	} else if ((b & 0xF8) == 0xF0) {
		n = 4;
		c = b & 0x07;
	} else {
		*cp = 0xFFFD;
		return 1;
	}
	if (len < n) {
		*cp = 0xFFFD;
		return len;
	}
	for (size_t i = 1; i < n; i++) {
		if ((s[i] & 0xC0) != 0x80) {
			*cp = 0xFFFD;
			return i;
		}
		c = (c << 6) | (s[i] & 0x3F);
	}
	*cp = c;
	return n;
}

uint8_t op_cp_to_cp1252(uint32_t cp)
{
	if (cp == '\n') {
		return '\n';
	}
	if (cp == '\t') {
		return ' ';
	}
	if (cp < 0x20) {
		return 0;
	}
	if (cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF)) {
		return cp == 0xA0 ? ' ' : (cp == 0xAD ? 0 : (uint8_t)cp);
	}
	for (int i = 0; i < 32; i++) {
		if (cp1252_high[i] != 0 && cp1252_high[i] == cp) {
			return (uint8_t)(0x80 + i);
		}
	}
	switch (cp) {
	case 0x2010: case 0x2011: case 0x2012: case 0x2015: case 0x2212:
		return '-';
	case 0x2002: case 0x2003: case 0x2007: case 0x2009: case 0x200A: case 0x202F:
		return ' ';
	case 0x2032:
		return '\'';
	case 0x2033:
		return '"';
	case 0x200B: case 0xFEFF:
		return 0;
	default:
		return '?';
	}
}

size_t op_utf8_to_cp1252(const char *src, size_t len, char *dst, size_t cap)
{
	size_t o = 0;
	size_t i = 0;

	if (cap == 0) {
		return 0;
	}
	while (i < len && o + 1 < cap) {
		uint32_t cp;
		size_t n = op_utf8_decode((const uint8_t *)src + i, len - i, &cp);

		i += n;
		if (cp == '\r') {
			continue;
		}
		uint8_t b = op_cp_to_cp1252(cp);

		if (b) {
			dst[o++] = (char)b;
		}
	}
	dst[o] = '\0';
	return o;
}

size_t op_utf8_count(const char *s, size_t len)
{
	size_t n = 0;

	for (size_t i = 0; i < len; i++) {
		if ((((uint8_t)s[i]) & 0xC0) != 0x80) {
			n++;
		}
	}
	return n;
}

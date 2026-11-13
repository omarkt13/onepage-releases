/*
 * OnePage Mini - text helpers. The device draws Windows-1252 glyphs only
 * (protocol section 6). UTF-8 from iOS and the calendar is mapped on arrival.
 */
#ifndef OP_TEXT_H
#define OP_TEXT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decodes one UTF-8 scalar. Returns bytes used (>= 1). Bad input gives U+FFFD. */
size_t op_utf8_decode(const uint8_t *s, size_t len, uint32_t *cp);
/* Maps one code point to a Windows-1252 byte, 0 to drop it, or '?' if unknown. */
uint8_t op_cp_to_cp1252(uint32_t cp);
/*
 * Converts UTF-8 to terminated Windows-1252. Newlines are kept, other control
 * characters dropped, tabs become spaces. Returns the length written.
 */
size_t op_utf8_to_cp1252(const char *src, size_t len, char *dst, size_t cap);
/* Number of Unicode scalar values in a UTF-8 string. */
size_t op_utf8_count(const char *s, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* OP_TEXT_H */

/*
 * OnePage Mini - portable helpers: checksums, little-endian access,
 * bounded readers and writers. Plain C99, no Zephyr headers.
 */
#ifndef OP_UTIL_H
#define OP_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OP_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define OP_MIN(a, b) ((a) < (b) ? (a) : (b))
#define OP_MAX(a, b) ((a) > (b) ? (a) : (b))

/* CRC-32 (IEEE, zlib). Pass 0 as crc for the first call; chain the result. */
uint32_t op_crc32(uint32_t crc, const void *data, size_t len);
/* CRC-16/CCITT-FALSE. Start with 0xFFFF. */
uint16_t op_crc16(uint16_t crc, const void *data, size_t len);

static inline uint16_t op_get_le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t op_get_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static inline void op_put_le16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static inline void op_put_le32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

/* Bounded reader. Any read past the end sets err and returns zero. */
struct op_rd {
	const uint8_t *p;
	size_t len;
	size_t pos;
	bool err;
};

static inline void op_rd_init(struct op_rd *r, const void *p, size_t len)
{
	r->p = (const uint8_t *)p;
	r->len = len;
	r->pos = 0;
	r->err = false;
}

static inline size_t op_rd_left(const struct op_rd *r)
{
	return r->err ? 0 : r->len - r->pos;
}

uint8_t op_rd_u8(struct op_rd *r);
uint16_t op_rd_u16(struct op_rd *r);
uint32_t op_rd_u32(struct op_rd *r);
/* Returns a pointer into the buffer, or NULL (and err) if too short. */
const uint8_t *op_rd_bytes(struct op_rd *r, size_t n);

/* Bounded writer. Any write past the end sets err. */
struct op_wr {
	uint8_t *p;
	size_t cap;
	size_t pos;
	bool err;
};

static inline void op_wr_init(struct op_wr *w, void *p, size_t cap)
{
	w->p = (uint8_t *)p;
	w->cap = cap;
	w->pos = 0;
	w->err = false;
}

static inline size_t op_wr_left(const struct op_wr *w)
{
	return w->err ? 0 : w->cap - w->pos;
}

void op_wr_u8(struct op_wr *w, uint8_t v);
void op_wr_u16(struct op_wr *w, uint16_t v);
void op_wr_u32(struct op_wr *w, uint32_t v);
void op_wr_bytes(struct op_wr *w, const void *p, size_t n);
/* TLV record: tag, len (<= 255), value. */
void op_wr_tlv(struct op_wr *w, uint8_t tag, const void *v, size_t n);
void op_wr_tlv_u8(struct op_wr *w, uint8_t tag, uint8_t v);
void op_wr_tlv_u16(struct op_wr *w, uint8_t tag, uint16_t v);
void op_wr_tlv_u32(struct op_wr *w, uint8_t tag, uint32_t v);

/* Copy at most dst_size-1 bytes and terminate. Never splits a UTF-8 sequence. */
size_t op_strlcpy_utf8(char *dst, size_t dst_size, const char *src, size_t src_len);

#ifdef __cplusplus
}
#endif

#endif /* OP_UTIL_H */

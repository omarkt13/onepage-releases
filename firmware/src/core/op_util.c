#include "op_util.h"

#include <string.h>

uint32_t op_crc32(uint32_t crc, const void *data, size_t len)
{
	const uint8_t *p = (const uint8_t *)data;

	crc = ~crc;
	while (len--) {
		crc ^= *p++;
		for (int k = 0; k < 8; k++) {
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
		}
	}
	return ~crc;
}

uint16_t op_crc16(uint16_t crc, const void *data, size_t len)
{
	const uint8_t *p = (const uint8_t *)data;

	while (len--) {
		crc ^= (uint16_t)(*p++) << 8;
		for (int k = 0; k < 8; k++) {
			crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
		}
	}
	return crc;
}

uint8_t op_rd_u8(struct op_rd *r)
{
	const uint8_t *p = op_rd_bytes(r, 1);

	return p ? p[0] : 0;
}

uint16_t op_rd_u16(struct op_rd *r)
{
	const uint8_t *p = op_rd_bytes(r, 2);

	return p ? op_get_le16(p) : 0;
}

uint32_t op_rd_u32(struct op_rd *r)
{
	const uint8_t *p = op_rd_bytes(r, 4);

	return p ? op_get_le32(p) : 0;
}

const uint8_t *op_rd_bytes(struct op_rd *r, size_t n)
{
	if (r->err || r->len - r->pos < n) {
		r->err = true;
		return NULL;
	}
	const uint8_t *p = r->p + r->pos;

	r->pos += n;
	return p;
}

void op_wr_bytes(struct op_wr *w, const void *p, size_t n)
{
	if (w->err || w->cap - w->pos < n) {
		w->err = true;
		return;
	}
	if (n) {
		memcpy(w->p + w->pos, p, n);
	}
	w->pos += n;
}

void op_wr_u8(struct op_wr *w, uint8_t v)
{
	op_wr_bytes(w, &v, 1);
}

void op_wr_u16(struct op_wr *w, uint16_t v)
{
	uint8_t b[2];

	op_put_le16(b, v);
	op_wr_bytes(w, b, 2);
}

void op_wr_u32(struct op_wr *w, uint32_t v)
{
	uint8_t b[4];

	op_put_le32(b, v);
	op_wr_bytes(w, b, 4);
}

void op_wr_tlv(struct op_wr *w, uint8_t tag, const void *v, size_t n)
{
	if (n > 255) {
		w->err = true;
		return;
	}
	op_wr_u8(w, tag);
	op_wr_u8(w, (uint8_t)n);
	op_wr_bytes(w, v, n);
}

void op_wr_tlv_u8(struct op_wr *w, uint8_t tag, uint8_t v)
{
	op_wr_tlv(w, tag, &v, 1);
}

void op_wr_tlv_u16(struct op_wr *w, uint8_t tag, uint16_t v)
{
	uint8_t b[2];

	op_put_le16(b, v);
	op_wr_tlv(w, tag, b, 2);
}

void op_wr_tlv_u32(struct op_wr *w, uint8_t tag, uint32_t v)
{
	uint8_t b[4];

	op_put_le32(b, v);
	op_wr_tlv(w, tag, b, 4);
}

size_t op_strlcpy_utf8(char *dst, size_t dst_size, const char *src, size_t src_len)
{
	if (dst_size == 0) {
		return 0;
	}
	size_t n = src_len < dst_size - 1 ? src_len : dst_size - 1;

	if (n < src_len) {
		/* Back off to the start of a UTF-8 sequence. */
		while (n > 0 && (((uint8_t)src[n]) & 0xC0) == 0x80) {
			n--;
		}
	}
	memcpy(dst, src, n);
	dst[n] = '\0';
	return n;
}

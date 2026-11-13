#include "calendar.h"

#include <string.h>

#include "op_proto.h"
#include "op_util.h"

void cal_init(struct cal_store *c)
{
	memset(c, 0, sizeof(*c));
}

bool cal_validate(const uint8_t *blob, uint32_t len, uint16_t *idx, uint16_t *count,
		  uint32_t *generated_at)
{
	struct op_rd r;

	if (len > CAL_MAX_BLOB) {
		return false;
	}
	op_rd_init(&r, blob, len);
	uint32_t gen = op_rd_u32(&r);
	uint16_t n = op_rd_u16(&r);

	if (r.err || n > CAL_MAX_EVENTS) {
		return false;
	}
	for (uint16_t i = 0; i < n; i++) {
		size_t at = r.pos;
		uint32_t start = op_rd_u32(&r);
		uint32_t end = op_rd_u32(&r);

		(void)op_rd_u8(&r);
		uint8_t tl = op_rd_u8(&r);

		(void)op_rd_bytes(&r, tl);
		uint8_t pl = op_rd_u8(&r);

		(void)op_rd_bytes(&r, pl);
		if (r.err || end < start || tl > CAL_TITLE_MAX_BYTES || pl > CAL_PLACE_MAX_BYTES) {
			return false;
		}
		if (idx) {
			idx[i] = (uint16_t)at;
		}
	}
	if (op_rd_left(&r) != 0) {
		return false;
	}
	if (count) {
		*count = n;
	}
	if (generated_at) {
		*generated_at = gen;
	}
	return true;
}

bool cal_load(struct cal_store *c, const uint8_t *blob, uint32_t len, uint32_t version)
{
	uint16_t idx[CAL_MAX_EVENTS];
	uint16_t n;
	uint32_t gen;

	if (!cal_validate(blob, len, idx, &n, &gen)) {
		return false;
	}
	memmove(c->blob, blob, len);
	memcpy(c->idx, idx, sizeof(idx));
	c->blob_len = len;
	c->count = n;
	c->generated_at = gen;
	c->version = version;
	return true;
}

void cal_erase(struct cal_store *c)
{
	c->blob_len = 0;
	c->count = 0;
	c->version = 0;
	c->generated_at = 0;
	c->staging = false;
	c->stage_received = 0;
	memset(c->blob, 0, sizeof(c->blob));
	/* A partial sync can hold event text too. */
	memset(c->stage, 0, sizeof(c->stage));
}

static struct cal_result res(uint8_t status, uint32_t version, uint32_t next)
{
	struct cal_result r = {.status = status, .version = version, .next_offset = next};

	return r;
}

struct cal_result cal_begin(struct cal_store *c, uint32_t version, uint32_t total_len,
			    uint32_t crc32)
{
	if (total_len > CAL_MAX_BLOB || total_len < 6) {
		c->staging = false;
		return res(total_len > CAL_MAX_BLOB ? OP_CAL_TOO_LARGE : OP_CAL_BAD_CONTENT,
			   version, 0);
	}
	/* The same transfer again: continue where it stopped. */
	if (c->staging && c->stage_version == version && c->stage_len == total_len &&
	    c->stage_crc == crc32) {
		return res(OP_CAL_OK, version, c->stage_received);
	}
	c->staging = true;
	c->stage_version = version;
	c->stage_len = total_len;
	c->stage_crc = crc32;
	c->stage_received = 0;
	return res(OP_CAL_OK, version, 0);
}

struct cal_result cal_data(struct cal_store *c, uint32_t offset, const uint8_t *data,
			   size_t len)
{
	if (!c->staging) {
		return res(OP_CAL_NO_BEGIN, 0, 0);
	}
	if (offset != c->stage_received || len > c->stage_len - c->stage_received) {
		return res(OP_CAL_OUT_OF_ORDER, c->stage_version, c->stage_received);
	}
	memcpy(c->stage + offset, data, len);
	c->stage_received += (uint32_t)len;
	return res(OP_CAL_OK, c->stage_version, c->stage_received);
}

struct cal_result cal_commit(struct cal_store *c, uint32_t version)
{
	if (!c->staging || version != c->stage_version) {
		return res(OP_CAL_NO_BEGIN, version, 0);
	}
	if (c->stage_received != c->stage_len ||
	    op_crc32(0, c->stage, c->stage_len) != c->stage_crc) {
		c->staging = false;
		return res(OP_CAL_CRC_MISMATCH, version, 0);
	}
	c->staging = false;
	if (!cal_load(c, c->stage, c->stage_len, version)) {
		return res(OP_CAL_BAD_CONTENT, version, 0);
	}
	struct cal_result r = res(OP_CAL_OK, version, c->stage_len);

	r.changed = true;
	return r;
}

struct cal_result cal_clear(struct cal_store *c)
{
	cal_erase(c);
	struct cal_result r = res(OP_CAL_OK, 0, 0);

	r.changed = true;
	return r;
}

bool cal_get(const struct cal_store *c, uint16_t i, struct cal_event *e)
{
	if (i >= c->count) {
		return false;
	}
	const uint8_t *p = c->blob + c->idx[i];

	e->start = op_get_le32(p);
	e->end = op_get_le32(p + 4);
	e->all_day = (p[8] & CAL_FLAG_ALL_DAY) != 0;
	e->title_len = p[9];
	e->title = (const char *)p + 10;
	e->place_len = p[10 + e->title_len];
	e->place = (const char *)p + 11 + e->title_len;
	return true;
}

uint16_t cal_first_current(const struct cal_store *c, uint32_t now)
{
	struct cal_event e;

	for (uint16_t i = 0; i < c->count; i++) {
		cal_get(c, i, &e);
		if (e.end > now) {
			return i;
		}
	}
	return c->count;
}

uint16_t cal_current_count(const struct cal_store *c, uint32_t now)
{
	struct cal_event e;
	uint16_t n = 0;

	for (uint16_t i = 0; i < c->count; i++) {
		cal_get(c, i, &e);
		if (e.end > now) {
			n++;
		}
	}
	return n;
}

uint16_t cal_current_nth(const struct cal_store *c, uint32_t now, uint16_t k)
{
	struct cal_event e;

	for (uint16_t i = 0; i < c->count; i++) {
		cal_get(c, i, &e);
		if (e.end > now) {
			if (k == 0) {
				return i;
			}
			k--;
		}
	}
	return c->count;
}

bool cal_next_event(const struct cal_store *c, uint32_t now, struct cal_event *out)
{
	struct cal_event e;
	bool have_all_day = false;
	struct cal_event all_day;

	for (uint16_t i = 0; i < c->count; i++) {
		cal_get(c, i, &e);
		if (e.end <= now) {
			continue;
		}
		if (!e.all_day) {
			*out = e;
			return true;
		}
		if (!have_all_day) {
			all_day = e;
			have_all_day = true;
		}
	}
	if (have_all_day) {
		*out = all_day;
	}
	return have_all_day;
}

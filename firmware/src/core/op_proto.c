#include "op_proto.h"

#include <string.h>

/* Checks the version byte and starts a reader after it. */
static int start_read(struct op_rd *r, const uint8_t *buf, size_t len)
{
	op_rd_init(r, buf, len);
	if (len < 1) {
		return OP_PARSE_MALFORMED;
	}
	if (op_rd_u8(r) != OP_PROTO_VERSION) {
		return OP_PARSE_BAD_VERSION;
	}
	return OP_PARSE_OK;
}

/* Walks TLV records. Returns false at the end; sets *bad on a truncated record. */
static bool next_tlv(struct op_rd *r, uint8_t *tag, const uint8_t **val, uint8_t *vlen, bool *bad)
{
	if (op_rd_left(r) == 0) {
		return false;
	}
	if (op_rd_left(r) < 2) {
		*bad = true;
		return false;
	}
	*tag = op_rd_u8(r);
	*vlen = op_rd_u8(r);
	*val = op_rd_bytes(r, *vlen);
	if (*val == NULL) {
		*bad = true;
		return false;
	}
	return true;
}

static void copy_str(char *dst, size_t dst_size, const uint8_t *src, size_t n)
{
	op_strlcpy_utf8(dst, dst_size, (const char *)src, n);
}

/* ---- Status ------------------------------------------------------------ */

size_t op_status_encode(const struct op_status *s, uint8_t *buf, size_t cap)
{
	struct op_wr w;

	op_wr_init(&w, buf, cap);
	op_wr_u8(&w, OP_PROTO_VERSION);
	op_wr_tlv_u8(&w, 0x01, s->battery);
	op_wr_tlv_u8(&w, 0x02, s->charging);
	op_wr_tlv(&w, 0x03, s->firmware, strnlen(s->firmware, sizeof(s->firmware)));
	op_wr_tlv_u32(&w, 0x04, s->storage_free_kib);
	op_wr_tlv_u32(&w, 0x05, s->storage_total_kib);
	op_wr_tlv_u16(&w, 0x06, s->last_error);
	op_wr_tlv_u32(&w, 0x07, s->calendar_version);
	op_wr_tlv_u32(&w, 0x08, s->calendar_synced_at);
	op_wr_tlv_u8(&w, 0x09, s->hw_rev);
	return w.err ? 0 : w.pos;
}

int op_status_decode(struct op_status *s, const uint8_t *buf, size_t len)
{
	struct op_rd r;
	int rc = start_read(&r, buf, len);
	uint8_t tag, vl;
	const uint8_t *v;
	bool bad = false;

	if (rc) {
		return rc;
	}
	memset(s, 0, sizeof(*s));
	while (next_tlv(&r, &tag, &v, &vl, &bad)) {
		switch (tag) {
		case 0x01: if (vl == 1) { s->battery = v[0]; } break;
		case 0x02: if (vl == 1) { s->charging = v[0]; } break;
		case 0x03: copy_str(s->firmware, sizeof(s->firmware), v, vl); break;
		case 0x04: if (vl == 4) { s->storage_free_kib = op_get_le32(v); } break;
		case 0x05: if (vl == 4) { s->storage_total_kib = op_get_le32(v); } break;
		case 0x06: if (vl == 2) { s->last_error = op_get_le16(v); } break;
		case 0x07: if (vl == 4) { s->calendar_version = op_get_le32(v); } break;
		case 0x08: if (vl == 4) { s->calendar_synced_at = op_get_le32(v); } break;
		case 0x09: if (vl == 1) { s->hw_rev = v[0]; } break;
		default: break;
		}
	}
	return bad ? OP_PARSE_MALFORMED : OP_PARSE_OK;
}

/* ---- Settings ---------------------------------------------------------- */

void op_settings_default(struct op_settings *s)
{
	memset(s, 0, sizeof(*s));
	s->quiet_on = 0;
	s->quiet_start = 22 * 60;
	s->quiet_end = 7 * 60;
	s->refresh_mode = OP_REFRESH_BALANCED;
	s->clock_12h = 0;
	s->language[0] = 'e';
	s->language[1] = 'n';
	s->alert_strength = 2;
}

size_t op_settings_encode(const struct op_settings *s, uint8_t *buf, size_t cap)
{
	struct op_wr w;

	op_wr_init(&w, buf, cap);
	op_wr_u8(&w, OP_PROTO_VERSION);
	op_wr_tlv_u8(&w, 0x01, s->quiet_on);
	op_wr_tlv_u16(&w, 0x02, s->quiet_start);
	op_wr_tlv_u16(&w, 0x03, s->quiet_end);
	op_wr_tlv_u8(&w, 0x04, s->refresh_mode);
	op_wr_tlv_u8(&w, 0x05, s->clock_12h);
	op_wr_tlv(&w, 0x06, s->language, 2);
	op_wr_tlv_u8(&w, 0x07, s->alert_strength);
	return w.err ? 0 : w.pos;
}

int op_settings_apply(struct op_settings *s, const uint8_t *buf, size_t len)
{
	struct op_rd r;
	int rc = start_read(&r, buf, len);
	uint8_t tag, vl;
	const uint8_t *v;
	bool bad = false;
	struct op_settings n = *s;

	if (rc) {
		return rc;
	}
	while (next_tlv(&r, &tag, &v, &vl, &bad)) {
		switch (tag) {
		case 0x01:
			if (vl == 1 && v[0] <= 1) { n.quiet_on = v[0]; }
			break;
		case 0x02:
			if (vl == 2 && op_get_le16(v) < 1440) { n.quiet_start = op_get_le16(v); }
			break;
		case 0x03:
			if (vl == 2 && op_get_le16(v) < 1440) { n.quiet_end = op_get_le16(v); }
			break;
		case 0x04:
			if (vl == 1 && v[0] <= OP_REFRESH_FAST) { n.refresh_mode = v[0]; }
			break;
		case 0x05:
			if (vl == 1 && v[0] <= 1) { n.clock_12h = v[0]; }
			break;
		case 0x06:
			if (vl == 2) { n.language[0] = (char)v[0]; n.language[1] = (char)v[1]; n.language[2] = 0; }
			break;
		case 0x07:
			if (vl == 1 && v[0] <= 3) { n.alert_strength = v[0]; }
			break;
		default:
			break;
		}
	}
	if (bad) {
		return OP_PARSE_MALFORMED;
	}
	*s = n;
	return OP_PARSE_OK;
}

/* ---- App filter -------------------------------------------------------- */

void op_filter_entry_write(struct op_wr *w, const struct op_filter_entry *e)
{
	size_t n = strnlen(e->id, OP_APP_ID_MAX);

	op_wr_u8(w, e->mode);
	op_wr_u8(w, e->pattern);
	op_wr_u8(w, e->flags);
	op_wr_u8(w, (uint8_t)n);
	op_wr_bytes(w, e->id, n);
}

size_t op_filter_entry_size(const struct op_filter_entry *e)
{
	return 4 + strnlen(e->id, OP_APP_ID_MAX);
}

bool op_filter_entry_read(struct op_rd *r, struct op_filter_entry *e)
{
	memset(e, 0, sizeof(*e));
	e->mode = op_rd_u8(r);
	e->pattern = op_rd_u8(r);
	e->flags = op_rd_u8(r);
	uint8_t n = op_rd_u8(r);
	const uint8_t *id = op_rd_bytes(r, n);

	if (r->err || n == 0 || n > OP_APP_ID_MAX || e->mode > OP_MODE_HIDE ||
	    e->pattern > OP_PATTERN_NONE) {
		return false;
	}
	memcpy(e->id, id, n);
	e->id[n] = '\0';
	return true;
}

int op_filter_cmd_parse(struct op_filter_cmd *c, const uint8_t *buf, size_t len)
{
	struct op_rd r;
	int rc = start_read(&r, buf, len);

	if (rc) {
		return rc;
	}
	memset(c, 0, sizeof(*c));
	c->op = op_rd_u8(&r);
	switch (c->op) {
	case OP_FILTER_LIST:
		c->start = op_rd_u16(&r);
		break;
	case OP_FILTER_SET:
		if (!op_filter_entry_read(&r, &c->entry)) {
			return OP_PARSE_MALFORMED;
		}
		break;
	case OP_FILTER_REMOVE: {
		uint8_t n = op_rd_u8(&r);
		const uint8_t *id = op_rd_bytes(&r, n);

		if (r.err || n == 0 || n > OP_APP_ID_MAX) {
			return OP_PARSE_MALFORMED;
		}
		memcpy(c->entry.id, id, n);
		c->entry.id[n] = '\0';
		break;
	}
	default:
		return OP_PARSE_MALFORMED;
	}
	return r.err ? OP_PARSE_MALFORMED : OP_PARSE_OK;
}

size_t op_filter_simple_reply(uint8_t op, uint8_t result, uint8_t *buf, size_t cap)
{
	if (cap < 3) {
		return 0;
	}
	buf[0] = OP_PROTO_VERSION;
	buf[1] = op;
	buf[2] = result;
	return 3;
}

/* ---- Home data --------------------------------------------------------- */

int op_weather_apply(struct op_weather *w, const uint8_t *buf, size_t len)
{
	struct op_rd r;
	int rc = start_read(&r, buf, len);
	uint8_t tag, vl;
	const uint8_t *v;
	bool bad = false;
	struct op_weather n = *w;

	if (rc) {
		return rc;
	}
	while (next_tlv(&r, &tag, &v, &vl, &bad)) {
		switch (tag) {
		case 0x01: if (vl == 2) { n.temp_now = (int16_t)op_get_le16(v); } break;
		case 0x02: if (vl == 2) { n.temp_high = (int16_t)op_get_le16(v); } break;
		case 0x03: if (vl == 2) { n.temp_low = (int16_t)op_get_le16(v); } break;
		case 0x04:
			if (vl == 1) { n.condition = v[0] < OP_WX_COUNT ? v[0] : OP_WX_UNKNOWN; }
			break;
		case 0x05: if (vl == 4) { n.valid_until = op_get_le32(v); } break;
		case 0x06: copy_str(n.place, sizeof(n.place), v, vl); break;
		case 0x07: if (vl == 1 && v[0] <= 1) { n.unit = v[0]; } break;
		default: break;
		}
	}
	if (bad) {
		return OP_PARSE_MALFORMED;
	}
	n.valid = true;
	*w = n;
	return OP_PARSE_OK;
}

size_t op_weather_encode(const struct op_weather *w, uint8_t *buf, size_t cap)
{
	struct op_wr wr;

	op_wr_init(&wr, buf, cap);
	op_wr_u8(&wr, OP_PROTO_VERSION);
	op_wr_tlv_u16(&wr, 0x01, (uint16_t)w->temp_now);
	op_wr_tlv_u16(&wr, 0x02, (uint16_t)w->temp_high);
	op_wr_tlv_u16(&wr, 0x03, (uint16_t)w->temp_low);
	op_wr_tlv_u8(&wr, 0x04, w->condition);
	op_wr_tlv_u32(&wr, 0x05, w->valid_until);
	op_wr_tlv(&wr, 0x06, w->place, strnlen(w->place, sizeof(w->place)));
	op_wr_tlv_u8(&wr, 0x07, w->unit);
	return wr.err ? 0 : wr.pos;
}

/* ---- Calendar ---------------------------------------------------------- */

int op_cal_cmd_parse(struct op_cal_cmd *c, const uint8_t *buf, size_t len)
{
	struct op_rd r;
	int rc = start_read(&r, buf, len);

	if (rc) {
		return rc;
	}
	memset(c, 0, sizeof(*c));
	c->op = op_rd_u8(&r);
	switch (c->op) {
	case OP_CAL_BEGIN:
		c->version = op_rd_u32(&r);
		c->total_len = op_rd_u32(&r);
		c->crc32 = op_rd_u32(&r);
		break;
	case OP_CAL_DATA:
		c->offset = op_rd_u32(&r);
		c->data_len = op_rd_left(&r);
		c->data = op_rd_bytes(&r, c->data_len);
		break;
	case OP_CAL_COMMIT:
		c->version = op_rd_u32(&r);
		break;
	case OP_CAL_CLEAR:
		break;
	default:
		return OP_PARSE_MALFORMED;
	}
	return r.err ? OP_PARSE_MALFORMED : OP_PARSE_OK;
}

size_t op_cal_ack_encode(uint8_t status, uint32_t version, uint32_t next_offset, uint8_t *buf,
			 size_t cap)
{
	struct op_wr w;

	op_wr_init(&w, buf, cap);
	op_wr_u8(&w, OP_PROTO_VERSION);
	op_wr_u8(&w, OP_CAL_ACK);
	op_wr_u8(&w, status);
	op_wr_u32(&w, version);
	op_wr_u32(&w, next_offset);
	return w.err ? 0 : w.pos;
}

/* ---- Data request ------------------------------------------------------ */

size_t op_data_request_encode(uint8_t kind, uint8_t reason, uint8_t *buf, size_t cap)
{
	if (cap < 3) {
		return 0;
	}
	buf[0] = OP_PROTO_VERSION;
	buf[1] = kind;
	buf[2] = reason;
	return 3;
}

/* ---- Privacy ----------------------------------------------------------- */

void op_privacy_default(struct op_privacy *p)
{
	p->show_calendar_away = 1;
}

size_t op_privacy_encode(const struct op_privacy *p, uint8_t *buf, size_t cap)
{
	struct op_wr w;

	op_wr_init(&w, buf, cap);
	op_wr_u8(&w, OP_PROTO_VERSION);
	op_wr_tlv_u8(&w, 0x01, p->show_calendar_away);
	return w.err ? 0 : w.pos;
}

int op_privacy_apply(struct op_privacy *p, bool *erase, const uint8_t *buf, size_t len)
{
	struct op_rd r;
	int rc = start_read(&r, buf, len);
	uint8_t tag, vl;
	const uint8_t *v;
	bool bad = false;
	struct op_privacy n = *p;

	*erase = false;
	if (rc) {
		return rc;
	}
	while (next_tlv(&r, &tag, &v, &vl, &bad)) {
		if (tag == 0x01 && vl == 1 && v[0] <= 1) {
			n.show_calendar_away = v[0];
		} else if (tag == 0xE0 && vl == 4 && memcmp(v, "ERAS", 4) == 0) {
			*erase = true;
		}
	}
	if (bad) {
		*erase = false;
		return OP_PARSE_MALFORMED;
	}
	*p = n;
	return OP_PARSE_OK;
}

/* ---- Transfer ---------------------------------------------------------- */

int op_xfer_cmd_parse(struct op_xfer_cmd *c, const uint8_t *buf, size_t len)
{
	struct op_rd r;
	int rc = start_read(&r, buf, len);

	if (rc) {
		return rc;
	}
	memset(c, 0, sizeof(*c));
	c->op = op_rd_u8(&r);
	switch (c->op) {
	case OP_XFER_START: {
		c->book_id = op_rd_u32(&r);
		c->total_len = op_rd_u32(&r);
		c->crc32 = op_rd_u32(&r);
		uint8_t n = op_rd_u8(&r);
		const uint8_t *t = op_rd_bytes(&r, n);

		if (r.err) {
			return OP_PARSE_MALFORMED;
		}
		copy_str(c->title, sizeof(c->title), t, n);
		break;
	}
	case OP_XFER_QUERY:
	case OP_XFER_CANCEL:
	case OP_XFER_DELETE:
	case OP_XFER_FINISH:
		c->book_id = op_rd_u32(&r);
		break;
	case OP_XFER_LIST:
		c->start = op_rd_u16(&r);
		break;
	case OP_XFER_FAST:
		c->fast_on = op_rd_u8(&r);
		break;
	default:
		return OP_PARSE_MALFORMED;
	}
	return r.err ? OP_PARSE_MALFORMED : OP_PARSE_OK;
}

int op_xfer_block_parse(struct op_xfer_block *b, const uint8_t *buf, size_t len)
{
	struct op_rd r;
	int rc = start_read(&r, buf, len);

	if (rc) {
		return rc;
	}
	b->offset = op_rd_u32(&r);
	b->crc16 = op_rd_u16(&r);
	b->len = op_rd_left(&r);
	b->data = op_rd_bytes(&r, b->len);
	return r.err ? OP_PARSE_MALFORMED : OP_PARSE_OK;
}

size_t op_xfer_progress_encode(uint32_t book_id, uint8_t status, uint32_t next_offset,
			       uint8_t *buf, size_t cap)
{
	struct op_wr w;

	op_wr_init(&w, buf, cap);
	op_wr_u8(&w, OP_PROTO_VERSION);
	op_wr_u8(&w, OP_XFER_PROGRESS);
	op_wr_u32(&w, book_id);
	op_wr_u8(&w, status);
	op_wr_u32(&w, next_offset);
	return w.err ? 0 : w.pos;
}

size_t op_xfer_result_encode(uint32_t book_id, uint8_t status, uint8_t *buf, size_t cap)
{
	struct op_wr w;

	op_wr_init(&w, buf, cap);
	op_wr_u8(&w, OP_PROTO_VERSION);
	op_wr_u8(&w, OP_XFER_RESULT);
	op_wr_u32(&w, book_id);
	op_wr_u8(&w, status);
	return w.err ? 0 : w.pos;
}

size_t op_xfer_list_encode(const struct op_book_info *books, uint16_t total, uint16_t start,
			   uint8_t *buf, size_t cap)
{
	struct op_wr w;
	uint8_t count = 0;

	op_wr_init(&w, buf, cap);
	op_wr_u8(&w, OP_PROTO_VERSION);
	op_wr_u8(&w, OP_XFER_LIST_REPLY);
	op_wr_u16(&w, total);
	op_wr_u16(&w, start);
	size_t count_pos = w.pos;

	op_wr_u8(&w, 0);
	if (w.err) {
		return 0;
	}
	for (uint16_t i = start; i < total && count < 255; i++) {
		const struct op_book_info *b = &books[i - start];
		size_t tl = strnlen(b->title, OP_BOOK_TITLE_MAX);

		if (op_wr_left(&w) < 13 + tl) {
			break;
		}
		op_wr_u32(&w, b->book_id);
		op_wr_u32(&w, b->size);
		op_wr_u32(&w, b->position);
		op_wr_u8(&w, (uint8_t)tl);
		op_wr_bytes(&w, b->title, tl);
		count++;
	}
	buf[count_pos] = count;
	return w.pos;
}

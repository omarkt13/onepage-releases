#include "app_filter.h"

#include <string.h>

void app_filter_init(struct app_filter *f)
{
	memset(f, 0, sizeof(*f));
}

static int find_idx(const struct app_filter *f, const char *id)
{
	for (int i = 0; i < f->n; i++) {
		if (strncmp(f->e[i].id, id, OP_APP_ID_MAX) == 0) {
			return i;
		}
	}
	return -1;
}

const struct op_filter_entry *app_filter_find(const struct app_filter *f, const char *id)
{
	int i = find_idx(f, id);

	return i < 0 ? NULL : &f->e[i];
}

static bool evictable(const struct op_filter_entry *e)
{
	return e->mode == OP_MODE_SHOW && e->flags == 0 && e->pattern == OP_PATTERN_DEFAULT;
}

static void remove_at(struct app_filter *f, int i)
{
	memmove(&f->e[i], &f->e[i + 1], (size_t)(f->n - i - 1) * sizeof(f->e[0]));
	f->n--;
	f->dirty = true;
}

/* Makes room for one entry. False if every entry is customised. */
static bool make_room(struct app_filter *f)
{
	if (f->n < APP_FILTER_MAX) {
		return true;
	}
	for (int i = 0; i < f->n; i++) {
		if (evictable(&f->e[i])) {
			remove_at(f, i);
			return true;
		}
	}
	return false;
}

static struct op_filter_entry *append(struct app_filter *f, const char *id)
{
	struct op_filter_entry *e = &f->e[f->n++];

	memset(e, 0, sizeof(*e));
	strncpy(e->id, id, OP_APP_ID_MAX);
	e->id[OP_APP_ID_MAX] = '\0';
	f->dirty = true;
	return e;
}

const struct op_filter_entry *app_filter_seen(struct app_filter *f, const char *id)
{
	static const struct op_filter_entry show_default = {0};
	int i = find_idx(f, id);

	if (i >= 0) {
		return &f->e[i];
	}
	if (id[0] == '\0' || !make_room(f)) {
		return &show_default;
	}
	return append(f, id);
}

uint8_t app_filter_set(struct app_filter *f, const struct op_filter_entry *e)
{
	int i = find_idx(f, e->id);
	struct op_filter_entry *d;

	if (i >= 0) {
		d = &f->e[i];
	} else {
		if (!make_room(f)) {
			return (uint8_t)OP_ERR_STORAGE_FULL;
		}
		d = append(f, e->id);
	}
	d->mode = e->mode;
	d->pattern = e->pattern;
	d->flags = e->flags;
	f->dirty = true;
	return 0;
}

uint8_t app_filter_remove(struct app_filter *f, const char *id)
{
	int i = find_idx(f, id);

	if (i >= 0) {
		remove_at(f, i);
	}
	return 0;
}

size_t app_filter_list_reply(const struct app_filter *f, uint16_t start, uint8_t *buf,
			     size_t cap)
{
	struct op_wr w;
	uint8_t count = 0;

	op_wr_init(&w, buf, cap);
	op_wr_u8(&w, OP_PROTO_VERSION);
	op_wr_u8(&w, OP_FILTER_LIST_REPLY);
	op_wr_u16(&w, f->n);
	op_wr_u16(&w, start);
	size_t count_pos = w.pos;

	op_wr_u8(&w, 0);
	if (w.err) {
		return 0;
	}
	for (uint16_t i = start; i < f->n; i++) {
		if (op_wr_left(&w) < op_filter_entry_size(&f->e[i])) {
			break;
		}
		op_filter_entry_write(&w, &f->e[i]);
		count++;
	}
	buf[count_pos] = count;
	return w.pos;
}

size_t app_filter_handle(struct app_filter *f, const uint8_t *req, size_t req_len, uint8_t *rsp,
			 size_t rsp_cap)
{
	struct op_filter_cmd c;

	if (op_filter_cmd_parse(&c, req, req_len) != OP_PARSE_OK) {
		return 0;
	}
	switch (c.op) {
	case OP_FILTER_LIST:
		return app_filter_list_reply(f, c.start, rsp, rsp_cap);
	case OP_FILTER_SET:
		return op_filter_simple_reply(OP_FILTER_SET_REPLY, app_filter_set(f, &c.entry), rsp,
					      rsp_cap);
	case OP_FILTER_REMOVE:
		return op_filter_simple_reply(OP_FILTER_REMOVE_REPLY,
					      app_filter_remove(f, c.entry.id), rsp, rsp_cap);
	default:
		return 0;
	}
}

#include "reader.h"

#include <string.h>

#include "op_util.h"

static int read_exact(reader_read_fn read, void *ctx, uint32_t off, uint8_t *buf, size_t len)
{
	int n = read(ctx, off, buf, len);

	return (n == (int)len) ? 0 : -1;
}

int opb_parse(struct opb_info *info, reader_read_fn read, void *ctx, uint32_t file_len)
{
	uint8_t h[OPB_HEADER_LEN];
	uint8_t n;
	uint32_t at = OPB_HEADER_LEN;

	memset(info, 0, sizeof(*info));
	if (file_len < OPB_HEADER_LEN || read_exact(read, ctx, 0, h, sizeof(h))) {
		return -1;
	}
	if (memcmp(h, "OPB1", 4) != 0 || h[4] != 1 || h[5] != 1) {
		return -1;
	}
	info->chapters = op_get_le16(h + 6);
	info->text_off = op_get_le32(h + 8);
	info->text_len = op_get_le32(h + 12);
	if (info->text_off > file_len || info->text_len > file_len - info->text_off) {
		return -1;
	}

	for (int field = 0; field < 2; field++) {
		char *dst = field == 0 ? info->title : info->author;

		if (read_exact(read, ctx, at, &n, 1) || n > 64 ||
		    read_exact(read, ctx, at + 1, (uint8_t *)dst, n)) {
			return -1;
		}
		dst[n] = '\0';
		at += 1u + n;
	}

	for (uint16_t i = 0; i < info->chapters; i++) {
		uint8_t e[5];

		if (at + 5 > info->text_off || read_exact(read, ctx, at, e, 5)) {
			return -1;
		}
		uint32_t off = op_get_le32(e);

		if (off > info->text_len) {
			return -1;
		}
		if (i < OPB_MAX_CHAPTERS) {
			info->chapter_off[i] = off;
			info->chapters_loaded = i + 1;
		}
		at += 5u + e[4];
	}
	return at <= info->text_off ? 0 : -1;
}

void reader_src_init(struct reader_src *s, reader_read_fn read, void *ctx, uint32_t text_off,
		     uint32_t text_len)
{
	memset(s, 0, sizeof(*s));
	s->read = read;
	s->ctx = ctx;
	s->text_off = text_off;
	s->text_len = text_len;
}

int reader_src_byte(struct reader_src *s, uint32_t at)
{
	if (at >= s->text_len) {
		return -1;
	}
	if (at < s->cache_at || at >= s->cache_at + s->cache_len) {
		uint32_t len = s->text_len - at;

		if (len > sizeof(s->cache)) {
			len = sizeof(s->cache);
		}
		int n = s->read(s->ctx, s->text_off + at, s->cache, len);

		if (n <= 0) {
			s->cache_len = 0;
			return -1;
		}
		s->cache_at = at;
		s->cache_len = (uint16_t)n;
	}
	return s->cache[at - s->cache_at];
}

size_t reader_src_copy(struct reader_src *s, uint32_t at, char *dst, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		int b = reader_src_byte(s, at + (uint32_t)i);

		if (b < 0) {
			break;
		}
		dst[i] = (char)b;
	}
	return i;
}

/* Lays out one line. Returns the offset after it (past a break). */
static uint32_t layout_line(struct reader_src *s, const struct reader_metrics *m, uint32_t at,
			    struct reader_line *line)
{
	uint32_t w = 0;
	uint32_t last_space = UINT32_MAX;
	uint32_t i = at;

	line->start = at;
	for (;;) {
		int b = reader_src_byte(s, i);

		if (b < 0) {
			line->len = (uint16_t)(i - at);
			return i;
		}
		if (b == '\n') {
			line->len = (uint16_t)(i - at);
			return i + 1;
		}
		uint8_t cw = m->width(m->width_ctx, (uint8_t)b);

		if (w + cw > m->width_px && i > at) {
			if (b == ' ') {
				line->len = (uint16_t)(i - at);
				return i + 1;
			}
			if (last_space != UINT32_MAX) {
				line->len = (uint16_t)(last_space - at);
				return last_space + 1;
			}
			/* One word wider than the line: break it. */
			line->len = (uint16_t)(i - at);
			return i;
		}
		if (b == ' ') {
			last_space = i;
		}
		w += cw;
		i++;
		if (i - at >= 0xFFFF) {
			line->len = (uint16_t)(i - at);
			return i;
		}
	}
}

void reader_layout(struct reader_src *s, const struct reader_metrics *m, uint32_t start,
		   struct reader_page *p)
{
	uint32_t at = start;
	uint8_t max = m->lines > READER_MAX_LINES ? READER_MAX_LINES : m->lines;

	p->start = start;
	p->nlines = 0;
	/* A page does not start with a blank line left over from the last page. */
	while (at < s->text_len && reader_src_byte(s, at) == ' ') {
		at++;
	}
	while (p->nlines < max && at < s->text_len) {
		at = layout_line(s, m, at, &p->line[p->nlines]);
		p->nlines++;
	}
	p->end = at;
}

void reader_open(struct reader *r, const struct opb_info *info, reader_read_fn read, void *ctx,
		 const struct reader_metrics *m, uint32_t position)
{
	memset(r, 0, sizeof(*r));
	r->info = info;
	r->m = *m;
	reader_src_init(&r->src, read, ctx, info->text_off, info->text_len);
	if (position >= info->text_len) {
		position = 0;
	}
	reader_layout(&r->src, &r->m, position, &r->page);
}

bool reader_next(struct reader *r)
{
	if (r->page.end >= r->src.text_len) {
		return false;
	}
	if (r->hist_n == READER_HISTORY) {
		memmove(r->history, r->history + 1, sizeof(r->history) - sizeof(r->history[0]));
		r->hist_n--;
	}
	r->history[r->hist_n++] = r->page.start;
	reader_layout(&r->src, &r->m, r->page.end, &r->page);
	return true;
}

static uint32_t chapter_start_before(const struct reader *r, uint32_t pos)
{
	uint32_t best = 0;

	for (uint16_t i = 0; i < r->info->chapters_loaded; i++) {
		if (r->info->chapter_off[i] < pos && r->info->chapter_off[i] >= best) {
			best = r->info->chapter_off[i];
		}
	}
	return best;
}

bool reader_prev(struct reader *r)
{
	uint32_t cur = r->page.start;

	if (cur == 0) {
		return false;
	}
	if (r->hist_n > 0) {
		reader_layout(&r->src, &r->m, r->history[--r->hist_n], &r->page);
		return true;
	}
	/*
	 * No history (opened mid-book): lay out forward from the chapter start
	 * and take the last page that starts before the current one.
	 */
	uint32_t at = chapter_start_before(r, cur);
	uint32_t prev = at;
	struct reader_page tmp;

	while (at < cur) {
		reader_layout(&r->src, &r->m, at, &tmp);
		if (tmp.end <= at) {
			break;
		}
		prev = at;
		at = tmp.end;
	}
	reader_layout(&r->src, &r->m, prev, &r->page);
	return true;
}

uint16_t reader_chapter(const struct reader *r)
{
	uint16_t c = 0;

	for (uint16_t i = 0; i < r->info->chapters_loaded; i++) {
		if (r->info->chapter_off[i] <= r->page.start) {
			c = i;
		}
	}
	return c;
}

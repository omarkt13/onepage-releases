/*
 * Coverage tests added in review pass 6: untrusted calendar input, the
 * calendar list with running long events, UTF-8 edge cases, reader paging
 * past its history, scheduler disconnects, and small reply encoders.
 */
#include <stdlib.h>
#include <string.h>

#include "alerts.h"
#include "calendar.h"
#include "op_proto.h"
#include "op_time.h"
#include "op_util.h"
#include "reader.h"
#include "testing.h"
#include "text.h"

/* ---- calendar blobs -------------------------------------------------------- */

struct ev {
	uint32_t start, end;
	uint8_t flags;
	const char *title;
};

static size_t blob(uint8_t *b, size_t cap, const struct ev *e, int n)
{
	struct op_wr w;

	op_wr_init(&w, b, cap);
	op_wr_u32(&w, 5);
	op_wr_u16(&w, (uint16_t)n);
	for (int i = 0; i < n; i++) {
		op_wr_u32(&w, e[i].start);
		op_wr_u32(&w, e[i].end);
		op_wr_u8(&w, e[i].flags);
		op_wr_u8(&w, (uint8_t)strlen(e[i].title));
		op_wr_bytes(&w, e[i].title, strlen(e[i].title));
		op_wr_u8(&w, 0);
	}
	return w.err ? 0 : w.pos;
}

static uint8_t B[CAL_MAX_BLOB + 64];

static void test_cal_validate_rejects(void)
{
	const struct ev one = {100, 200, 0, "A"};
	size_t n = blob(B, sizeof(B), &one, 1);
	uint16_t count = 0;

	CHECK(n > 0);
	CHECK(cal_validate(B, (uint32_t)n, NULL, &count, NULL));
	CHECK_EQ(count, 1);
	/* Every truncation is refused. */
	for (size_t cut = 0; cut < n; cut++) {
		CHECK(!cal_validate(B, (uint32_t)cut, NULL, NULL, NULL));
	}
	/* Trailing bytes are refused. */
	B[n] = 0;
	CHECK(!cal_validate(B, (uint32_t)n + 1, NULL, NULL, NULL));
	/* End before start. */
	const struct ev back = {200, 100, 0, "A"};

	n = blob(B, sizeof(B), &back, 1);
	CHECK(!cal_validate(B, (uint32_t)n, NULL, NULL, NULL));
	/* Title longer than 160 bytes. */
	char longt[200];

	memset(longt, 'x', sizeof(longt) - 1);
	longt[sizeof(longt) - 1] = '\0';
	const struct ev lt = {100, 200, 0, longt};

	n = blob(B, sizeof(B), &lt, 1);
	CHECK(!cal_validate(B, (uint32_t)n, NULL, NULL, NULL));
	/* Count above 100. */
	struct op_wr w;

	op_wr_init(&w, B, sizeof(B));
	op_wr_u32(&w, 0);
	op_wr_u16(&w, CAL_MAX_EVENTS + 1);
	CHECK(!cal_validate(B, (uint32_t)w.pos, NULL, NULL, NULL));
	/* Over the size limit. */
	CHECK(!cal_validate(B, CAL_MAX_BLOB + 1, NULL, NULL, NULL));
	/* Empty week is valid. */
	n = blob(B, sizeof(B), NULL, 0);
	CHECK(cal_validate(B, (uint32_t)n, NULL, &count, NULL));
	CHECK_EQ(count, 0);
}

static struct cal_store C;

static void test_cal_running_all_day(void)
{
	/* An all-day event runs while two shorter ones have already ended. */
	const uint32_t day = 10 * OP_SECS_PER_DAY;
	const struct ev e[] = {
		{day, day + OP_SECS_PER_DAY, CAL_FLAG_ALL_DAY, "Holiday"},
		{day + 9 * 3600, day + 10 * 3600, 0, "Ended 1"},
		{day + 11 * 3600, day + 12 * 3600, 0, "Ended 2"},
		{day + 15 * 3600, day + 16 * 3600, 0, "Later"},
	};
	size_t n = blob(B, sizeof(B), e, 4);
	const uint32_t now = day + 13 * 3600;
	struct cal_event ce;

	cal_init(&C);
	CHECK(cal_load(&C, B, (uint32_t)n, 1));
	CHECK_EQ(cal_current_count(&C, now), 2);
	CHECK_EQ(cal_first_current(&C, now), 0);
	CHECK_EQ(cal_current_nth(&C, now, 0), 0);
	CHECK_EQ(cal_current_nth(&C, now, 1), 3); /* skips the two ended events */
	CHECK_EQ(cal_current_nth(&C, now, 2), 4); /* none: returns count */
	CHECK(cal_get(&C, cal_current_nth(&C, now, 1), &ce));
	CHECK(ce.title_len == 5 && memcmp(ce.title, "Later", 5) == 0);
	/* Home shows the timed event before the all-day one. */
	CHECK(cal_next_event(&C, now, &ce));
	CHECK(memcmp(ce.title, "Later", 5) == 0);
	/* Erase also clears a partial sync. */
	cal_begin(&C, 2, (uint32_t)n, op_crc32(0, B, n));
	cal_data(&C, 0, B, 20);
	cal_erase(&C);
	CHECK_EQ(cal_count(&C), 0);
	CHECK(!C.staging);
	CHECK_EQ(C.stage[10], 0);
	CHECK_EQ(cal_data(&C, 20, B + 20, 10).status, OP_CAL_NO_BEGIN);
}

/* ---- UTF-8 ------------------------------------------------------------------- */

static void test_utf8_edges(void)
{
	uint32_t cp;
	const uint8_t euro[] = {0xE2, 0x82, 0xAC};
	const uint8_t emoji[] = {0xF0, 0x9F, 0x8E, 0x89};
	const uint8_t bad_cont[] = {0xC3, 0x41};
	const uint8_t lone[] = {0x80};

	CHECK_EQ(op_utf8_decode(euro, 3, &cp), 3);
	CHECK_EQ(cp, 0x20AC);
	CHECK_EQ(op_cp_to_cp1252(cp), 0x80);
	CHECK_EQ(op_utf8_decode(emoji, 4, &cp), 4);
	CHECK_EQ(cp, 0x1F389);
	CHECK_EQ(op_cp_to_cp1252(cp), '?');
	CHECK_EQ(op_utf8_decode(euro, 2, &cp), 2); /* truncated */
	CHECK_EQ(cp, 0xFFFD);
	CHECK_EQ(op_utf8_decode(bad_cont, 2, &cp), 1); /* resync on 'A' */
	CHECK_EQ(cp, 0xFFFD);
	CHECK_EQ(op_utf8_decode(lone, 1, &cp), 1);
	CHECK_EQ(cp, 0xFFFD);

	CHECK_EQ(op_utf8_count("caf\xC3\xA9 \xF0\x9F\x8E\x89", 10), 6);

	/* Copy never splits a multi-byte sequence. */
	char dst[6];
	const char *s = "ab\xE2\x82\xAC" "cd"; /* "ab€cd" */

	CHECK_EQ(op_strlcpy_utf8(dst, sizeof(dst), s, strlen(s)), 5);
	CHECK_STR(dst, "ab\xE2\x82\xAC");
	CHECK_EQ(op_strlcpy_utf8(dst, 4, s, strlen(s)), 2);
	CHECK_STR(dst, "ab");
	CHECK_EQ(op_strlcpy_utf8(dst, 0, s, strlen(s)), 0);

	/* Bad bytes in a notification do not stop the conversion. */
	char out[16];
	size_t len = op_utf8_to_cp1252("a\xC3" "b\tc", 5, out, sizeof(out));

	CHECK(len >= 3);
	CHECK_EQ(out[len], '\0');
	CHECK(strchr(out, '\t') == NULL);
}

/* ---- reader --------------------------------------------------------------- */

struct mem {
	const uint8_t *p;
	size_t n;
	int reads;
};

static int mem_read(void *ctx, uint32_t off, uint8_t *buf, size_t len)
{
	struct mem *m = ctx;

	m->reads++;
	if (off >= m->n) {
		return 0;
	}
	if (len > m->n - off) {
		len = m->n - off;
	}
	memcpy(buf, m->p + off, len);
	return (int)len;
}

static uint8_t width10(void *ctx, uint8_t ch)
{
	(void)ctx;
	(void)ch;
	return 10;
}

static uint8_t BOOK[8192];

static size_t make_book(size_t text_len)
{
	struct op_wr w;
	const char *words = "lorem ipsum dolor sit amet consectetur ";
	size_t wl = strlen(words);

	op_wr_init(&w, BOOK, sizeof(BOOK));
	op_wr_bytes(&w, "OPB1", 4);
	op_wr_u8(&w, 1);
	op_wr_u8(&w, 1);
	op_wr_u16(&w, 1);
	op_wr_u32(&w, 16 + 2 + 2 + 5 + 2); /* text_off */
	op_wr_u32(&w, (uint32_t)text_len);
	op_wr_u8(&w, 1);
	op_wr_bytes(&w, "T", 1);
	op_wr_u8(&w, 1);
	op_wr_bytes(&w, "A", 1);
	op_wr_u32(&w, 0);
	op_wr_u8(&w, 2);
	op_wr_bytes(&w, "C1", 2);
	for (size_t i = 0; i < text_len; i++) {
		op_wr_u8(&w, (uint8_t)words[i % wl]);
	}
	return w.err ? 0 : w.pos;
}

static struct opb_info info;
static struct reader rd;

static void test_reader_long_book(void)
{
	size_t n = make_book(6000);
	struct mem m = {BOOK, n, 0};
	const struct reader_metrics met = {.width_px = 200, .lines = 2, .width = width10};
	uint32_t starts[400];
	int pages = 0;

	CHECK(n > 0);
	CHECK_EQ(opb_parse(&info, mem_read, &m, (uint32_t)n), 0);
	reader_open(&rd, &info, mem_read, &m, &met, 0);
	starts[pages++] = reader_position(&rd);
	while (reader_next(&rd) && pages < 400) {
		CHECK(reader_position(&rd) > starts[pages - 1]); /* always moves forward */
		starts[pages++] = reader_position(&rd);
	}
	CHECK(pages > READER_HISTORY + 10);
	CHECK_EQ(rd.page.end, info.text_len);
	/* Back past the 32-page history: every step goes back, and it reaches 0. */
	int back = 0;
	uint32_t prev = reader_position(&rd);

	while (reader_prev(&rd) && back < 1000) {
		CHECK(reader_position(&rd) < prev);
		prev = reader_position(&rd);
		back++;
	}
	CHECK_EQ(reader_position(&rd), 0);
	/* The first 32 steps back hit the exact pages seen going forward. */
	reader_open(&rd, &info, mem_read, &m, &met, 0);
	while (reader_next(&rd)) {
	}
	for (int i = 1; i <= READER_HISTORY; i++) {
		CHECK(reader_prev(&rd));
		CHECK_EQ(reader_position(&rd), starts[pages - 1 - i]);
	}
	/* A saved position past the end opens at the start. */
	reader_open(&rd, &info, mem_read, &m, &met, info.text_len + 5);
	CHECK_EQ(reader_position(&rd), 0);
}

static void test_reader_long_word_and_bad_files(void)
{
	size_t n = make_book(10);
	struct mem m = {BOOK, n, 0};
	const struct reader_metrics met = {.width_px = 50, .lines = 4, .width = width10};
	struct reader_page p;
	struct reader_src src;

	/* Text with no spaces is broken at the line width. */
	CHECK_EQ(opb_parse(&info, mem_read, &m, (uint32_t)n), 0);
	memset(BOOK + info.text_off, 'w', 10);
	reader_src_init(&src, mem_read, &m, info.text_off, info.text_len);
	reader_layout(&src, &met, 0, &p);
	CHECK_EQ(p.nlines, 2);
	CHECK_EQ(p.line[0].len, 5);
	CHECK_EQ(p.line[1].start, 5);
	CHECK_EQ(p.end, 10);

	/* Damaged files are refused, not read past their end. */
	CHECK(opb_parse(&info, mem_read, &m, 10) != 0);              /* shorter than header */
	BOOK[8] = 0xFF;                                              /* text_off past end */
	CHECK(opb_parse(&info, mem_read, &m, (uint32_t)n) != 0);
	n = make_book(10);
	BOOK[16] = 65;                                               /* title over 64 bytes */
	CHECK(opb_parse(&info, mem_read, &m, (uint32_t)n) != 0);
	n = make_book(10);
	op_put_le32(BOOK + 20, 11);                                  /* chapter past text */
	CHECK(opb_parse(&info, mem_read, &m, (uint32_t)n) != 0);
	n = make_book(10);
	BOOK[24] = 200;                                              /* chapter name overruns */
	CHECK(opb_parse(&info, mem_read, &m, (uint32_t)n) != 0);
	BOOK[24] = 2;
	BOOK[5] = 2;                                                 /* unknown charset */
	CHECK(opb_parse(&info, mem_read, &m, (uint32_t)n) != 0);
}

/* ---- scheduler and encoders ----------------------------------------------- */

static void test_sched_disconnect(void)
{
	struct sched s;
	uint32_t local = op_time_from_civil(2026, 11, 13, 10, 0, 0);

	sched_init(&s);
	sched_connected(&s, 0, local);
	sched_disconnected(&s);
	/* Nothing is asked for while away, even after hours. */
	CHECK_EQ(sched_tick(&s, 5 * 3600, true, local + 5 * 3600, 0).kind, 0);
	CHECK_EQ(sched_next_s(&s, 5 * 3600, true, local), UINT32_MAX);
	/* Reconnection asks for both, with the reconnect reason. */
	struct sched_req q = sched_connected(&s, 5 * 3600, local + 5 * 3600);

	CHECK_EQ(q.kind, OP_REQ_BOTH);
	CHECK_EQ(q.reason, OP_REASON_RECONNECT);
	/* Without a valid clock no calendar slot fires. */
	q = sched_tick(&s, 5 * 3600 + 60, false, 0, 0);
	CHECK_EQ(q.kind & OP_REQ_CALENDAR, 0);
}

static void test_small_encoders(void)
{
	uint8_t b[16];
	const uint8_t want_res[] = {1, 0x86, 0xD4, 0xC3, 0xB2, 0xA1, 2};
	const uint8_t want_set[] = {1, 0x82, 0};

	CHECK_MEM(b, op_xfer_result_encode(0xA1B2C3D4, 2, b, sizeof(b)), want_res,
		  sizeof(want_res));
	CHECK_EQ(op_xfer_result_encode(1, 0, b, 6), 0); /* too small */
	CHECK_MEM(b, op_filter_simple_reply(OP_FILTER_SET_REPLY, 0, b, sizeof(b)), want_set,
		  sizeof(want_set));
	CHECK_EQ(op_filter_simple_reply(OP_FILTER_SET_REPLY, 0, b, 2), 0);

	char buf[40];
	uint32_t midnight = op_time_from_civil(2026, 11, 13, 0, 7, 0);
	uint32_t noon = op_time_from_civil(2026, 11, 13, 12, 0, 0);

	op_fmt_clock(midnight, true, buf, sizeof(buf));
	CHECK_STR(buf, "12:07 am");
	op_fmt_clock(noon, true, buf, sizeof(buf));
	CHECK_STR(buf, "12:00 pm");
	op_fmt_date_long(noon, buf, sizeof(buf));
	CHECK_STR(buf, "Friday 13 November");
	/* Short buffers are cut, not overrun. */
	CHECK_EQ(op_fmt_date_long(noon, buf, 6), 5);
	CHECK_EQ(buf[5], '\0');
}

int main(void)
{
	static const struct test_case cases[] = {
		{"cal_validate_rejects", test_cal_validate_rejects},
		{"cal_running_all_day", test_cal_running_all_day},
		{"utf8_edges", test_utf8_edges},
		{"reader_long_book", test_reader_long_book},
		{"reader_long_word_bad_files", test_reader_long_word_and_bad_files},
		{"sched_disconnect", test_sched_disconnect},
		{"small_encoders", test_small_encoders},
	};
	return t_run(cases, (int)(sizeof(cases) / sizeof(cases[0])));
}

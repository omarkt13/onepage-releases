/*
 * Checks every firmware codec against shared/test-vectors/vectors.json, the
 * output of the reference encoder shared/tools/opproto.py.
 */
#include <stdlib.h>
#include <string.h>

#include "app_filter.h"
#include "calendar.h"
#include "mini_json.h"
#include "op_proto.h"
#include "reader.h"
#include "testing.h"
#include "text.h"

static struct mj *V;

static void test_crc(void)
{
	const struct mj *arr = mj_get(V, "crc");

	for (size_t i = 0; i < arr->n; i++) {
		char p[32];
		size_t n;

		snprintf(p, sizeof(p), "crc.%zu.input", i);
		uint8_t *in = mj_hex(V, p, &n);

		snprintf(p, sizeof(p), "crc.%zu.crc32", i);
		CHECK_EQ(op_crc32(0, in, n), mj_num(V, p));
		snprintf(p, sizeof(p), "crc.%zu.crc16", i);
		CHECK_EQ(op_crc16(0xFFFF, in, n), mj_num(V, p));
		/* Chained CRC-32 gives the same result. */
		if (n > 3) {
			snprintf(p, sizeof(p), "crc.%zu.crc32", i);
			CHECK_EQ(op_crc32(op_crc32(0, in, 3), in + 3, n - 3), mj_num(V, p));
		}
		free(in);
	}
}

static void test_status(void)
{
	struct op_status s = {0};
	uint8_t buf[128];
	size_t n;
	uint8_t *want = mj_hex(V, "status.bytes", &n);

	s.battery = (uint8_t)mj_num(V, "status.fields.battery");
	s.charging = (uint8_t)mj_num(V, "status.fields.charging");
	strcpy(s.firmware, mj_str(V, "status.fields.firmware"));
	s.storage_free_kib = (uint32_t)mj_num(V, "status.fields.storage_free_kib");
	s.storage_total_kib = (uint32_t)mj_num(V, "status.fields.storage_total_kib");
	s.last_error = (uint16_t)mj_num(V, "status.fields.last_error");
	s.calendar_version = (uint32_t)mj_num(V, "status.fields.calendar_version");
	s.calendar_synced_at = (uint32_t)mj_num(V, "status.fields.calendar_synced_at");
	s.hw_rev = (uint8_t)mj_num(V, "status.fields.hw_rev");

	size_t len = op_status_encode(&s, buf, sizeof(buf));

	CHECK_MEM(buf, len, want, n);

	struct op_status d;

	CHECK_EQ(op_status_decode(&d, want, n), OP_PARSE_OK);
	CHECK(memcmp(&d, &s, sizeof(s)) == 0);
	/* Wrong version is refused. */
	want[0] = 2;
	CHECK_EQ(op_status_decode(&d, want, n), OP_PARSE_BAD_VERSION);
	free(want);
}

static void test_settings(void)
{
	struct op_settings s;
	uint8_t buf[64];
	size_t n;
	uint8_t *want = mj_hex(V, "settings.bytes", &n);

	op_settings_default(&s);
	s.quiet_on = (uint8_t)mj_num(V, "settings.fields.quiet_on");
	s.quiet_start = (uint16_t)mj_num(V, "settings.fields.quiet_start");
	s.quiet_end = (uint16_t)mj_num(V, "settings.fields.quiet_end");
	s.refresh_mode = (uint8_t)mj_num(V, "settings.fields.refresh_mode");
	s.clock_12h = (uint8_t)mj_num(V, "settings.fields.clock_12h");
	s.alert_strength = (uint8_t)mj_num(V, "settings.fields.alert_strength");
	size_t len = op_settings_encode(&s, buf, sizeof(buf));

	CHECK_MEM(buf, len, want, n);

	struct op_settings d;

	op_settings_default(&d);
	d.quiet_on = 0;
	d.clock_12h = 1;
	CHECK_EQ(op_settings_apply(&d, want, n), OP_PARSE_OK);
	CHECK(memcmp(&d, &s, sizeof(s)) == 0);
	free(want);

	/* Partial write with an unknown tag: only alert strength changes. */
	uint8_t *partial = mj_hex(V, "settings.partial_with_unknown", &n);
	struct op_settings before = s;

	CHECK_EQ(op_settings_apply(&s, partial, n), OP_PARSE_OK);
	CHECK_EQ(s.alert_strength, mj_num(V, "settings.partial_expect.alert_strength"));
	s.alert_strength = before.alert_strength;
	CHECK(memcmp(&s, &before, sizeof(s)) == 0);
	/* Truncated TLV is refused and changes nothing. */
	CHECK_EQ(op_settings_apply(&s, partial, n - 1), OP_PARSE_MALFORMED);
	CHECK(memcmp(&s, &before, sizeof(s)) == 0);
	free(partial);
}

static void test_app_filter(void)
{
	size_t n;
	uint8_t buf[256];
	struct op_wr w;
	struct op_filter_entry e = {0};
	uint8_t *want = mj_hex(V, "app_filter.entry_bytes", &n);

	e.mode = (uint8_t)mj_num(V, "app_filter.entry.mode");
	e.pattern = (uint8_t)mj_num(V, "app_filter.entry.pattern");
	e.flags = (uint8_t)mj_num(V, "app_filter.entry.flags");
	strcpy(e.id, mj_str(V, "app_filter.entry.id"));
	op_wr_init(&w, buf, sizeof(buf));
	op_filter_entry_write(&w, &e);
	CHECK_MEM(buf, w.pos, want, n);
	free(want);

	struct op_filter_cmd c;
	uint8_t *set = mj_hex(V, "app_filter.set_bytes", &n);

	CHECK_EQ(op_filter_cmd_parse(&c, set, n), OP_PARSE_OK);
	CHECK_EQ(c.op, OP_FILTER_SET);
	CHECK(memcmp(&c.entry, &e, sizeof(e)) == 0);

	struct app_filter f;

	app_filter_init(&f);
	uint8_t rsp[64];
	size_t rl = app_filter_handle(&f, set, n, rsp, sizeof(rsp));

	CHECK_EQ(rl, 3);
	CHECK_EQ(rsp[1], OP_FILTER_SET_REPLY);
	CHECK_EQ(rsp[2], 0);
	CHECK_EQ(app_filter_find(&f, "com.example.bank")->flags, 1);
	free(set);

	uint8_t *lr = mj_hex(V, "app_filter.list_request_bytes", &n);

	CHECK_EQ(op_filter_cmd_parse(&c, lr, n), OP_PARSE_OK);
	CHECK_EQ(c.op, OP_FILTER_LIST);
	CHECK_EQ(c.start, 0);
	free(lr);

	/* Build the list from the vector entries and compare the reply. */
	app_filter_init(&f);
	const struct mj *ents = mj_get(V, "app_filter.list_reply.entries");

	for (size_t i = 0; i < ents->n; i++) {
		struct op_filter_entry x = {0};
		char p[64];

		snprintf(p, sizeof(p), "app_filter.list_reply.entries.%zu.mode", i);
		x.mode = (uint8_t)mj_num(V, p);
		snprintf(p, sizeof(p), "app_filter.list_reply.entries.%zu.pattern", i);
		x.pattern = (uint8_t)mj_num(V, p);
		snprintf(p, sizeof(p), "app_filter.list_reply.entries.%zu.flags", i);
		x.flags = (uint8_t)mj_num(V, p);
		snprintf(p, sizeof(p), "app_filter.list_reply.entries.%zu.id", i);
		strcpy(x.id, mj_str(V, p));
		app_filter_set(&f, &x);
	}
	want = mj_hex(V, "app_filter.list_reply.bytes", &n);
	size_t len = app_filter_list_reply(&f, 0, buf, sizeof(buf));

	CHECK_MEM(buf, len, want, n);
	/* A small MTU sends fewer entries, still a valid reply. */
	len = app_filter_list_reply(&f, 0, buf, 8 + 4 + 19);
	CHECK_EQ(buf[6], 1);
	free(want);
}

static void test_home_data(void)
{
	size_t n;
	uint8_t *want = mj_hex(V, "home_data.bytes", &n);
	struct op_weather w = {0};
	uint8_t buf[64];

	CHECK_EQ(op_weather_apply(&w, want, n), OP_PARSE_OK);
	CHECK(w.valid);
	CHECK_EQ(w.temp_now, mj_num(V, "home_data.fields.temp_now"));
	CHECK_EQ(w.temp_high, mj_num(V, "home_data.fields.temp_high"));
	CHECK_EQ(w.temp_low, mj_num(V, "home_data.fields.temp_low"));
	CHECK_EQ(w.condition, mj_num(V, "home_data.fields.condition"));
	CHECK_EQ(w.valid_until, mj_num(V, "home_data.fields.valid_until"));
	CHECK_STR(w.place, mj_str(V, "home_data.fields.place"));
	CHECK_EQ(w.unit, mj_num(V, "home_data.fields.unit"));
	size_t len = op_weather_encode(&w, buf, sizeof(buf));

	CHECK_MEM(buf, len, want, n);
	free(want);
}

static struct cal_store cal;

static void test_calendar(void)
{
	size_t n, bl;
	struct op_cal_cmd c;
	uint8_t *blob = mj_hex(V, "calendar.blob", &bl);
	uint32_t crc = (uint32_t)mj_num(V, "calendar.crc32");

	CHECK_EQ(op_crc32(0, blob, bl), crc);

	uint8_t *begin = mj_hex(V, "calendar.begin_bytes", &n);

	CHECK_EQ(op_cal_cmd_parse(&c, begin, n), OP_PARSE_OK);
	CHECK_EQ(c.op, OP_CAL_BEGIN);
	CHECK_EQ(c.version, 7);
	CHECK_EQ(c.total_len, bl);
	CHECK_EQ(c.crc32, crc);
	cal_init(&cal);
	struct cal_result r = cal_begin(&cal, c.version, c.total_len, c.crc32);

	CHECK_EQ(r.status, OP_CAL_OK);
	free(begin);

	uint8_t *data = mj_hex(V, "calendar.data_first_bytes", &n);

	CHECK_EQ(op_cal_cmd_parse(&c, data, n), OP_PARSE_OK);
	CHECK_EQ(c.op, OP_CAL_DATA);
	CHECK_EQ(c.offset, 0);
	CHECK_EQ(c.data_len, 100);
	CHECK(memcmp(c.data, blob, 100) == 0);
	r = cal_data(&cal, c.offset, c.data, c.data_len);
	CHECK_EQ(r.status, OP_CAL_OK);
	r = cal_data(&cal, 100, blob + 100, bl - 100);
	CHECK_EQ(r.status, OP_CAL_OK);
	free(data);

	uint8_t *commit = mj_hex(V, "calendar.commit_bytes", &n);

	CHECK_EQ(op_cal_cmd_parse(&c, commit, n), OP_PARSE_OK);
	CHECK_EQ(c.op, OP_CAL_COMMIT);
	r = cal_commit(&cal, c.version);
	CHECK_EQ(r.status, OP_CAL_OK);
	CHECK(r.changed);
	free(commit);

	uint8_t ack[16];
	size_t al = op_cal_ack_encode(r.status, r.version, r.next_offset, ack, sizeof(ack));
	uint8_t *want = mj_hex(V, "calendar.ack_ok_bytes", &n);

	CHECK_MEM(ack, al, want, n);
	free(want);

	CHECK_EQ(cal.generated_at, mj_num(V, "calendar.generated_at"));
	const struct mj *evs = mj_get(V, "calendar.events_out");

	CHECK_EQ(cal_count(&cal), evs->n);
	for (uint16_t i = 0; i < evs->n; i++) {
		struct cal_event e;
		char p[64];

		CHECK(cal_get(&cal, i, &e));
		snprintf(p, sizeof(p), "calendar.events_out.%u.start", i);
		CHECK_EQ(e.start, mj_num(V, p));
		snprintf(p, sizeof(p), "calendar.events_out.%u.end", i);
		CHECK_EQ(e.end, mj_num(V, p));
		snprintf(p, sizeof(p), "calendar.events_out.%u.all_day", i);
		CHECK_EQ(e.all_day, mj_num(V, p));
		snprintf(p, sizeof(p), "calendar.events_out.%u.title", i);
		const char *t = mj_str(V, p);

		CHECK_MEM(e.title, e.title_len, t, strlen(t));
		CHECK(e.title_len <= CAL_TITLE_MAX_BYTES);
		snprintf(p, sizeof(p), "calendar.events_out.%u.place", i);
		const char *pl = mj_str(V, p);

		CHECK_MEM(e.place, e.place_len, pl, strlen(pl));
	}
	free(blob);
}

static void test_data_request(void)
{
	const struct mj *arr = mj_get(V, "data_request");

	for (size_t i = 0; i < arr->n; i++) {
		char p[48];
		size_t n;
		uint8_t buf[8];

		snprintf(p, sizeof(p), "data_request.%zu.bytes", i);
		uint8_t *want = mj_hex(V, p, &n);

		snprintf(p, sizeof(p), "data_request.%zu.kind", i);
		uint8_t kind = (uint8_t)mj_num(V, p);

		snprintf(p, sizeof(p), "data_request.%zu.reason", i);
		uint8_t reason = (uint8_t)mj_num(V, p);
		size_t len = op_data_request_encode(kind, reason, buf, sizeof(buf));

		CHECK_MEM(buf, len, want, n);
		free(want);
	}
}

static void test_privacy_codec(void)
{
	size_t n;
	uint8_t buf[16];
	struct op_privacy p;
	bool erase;

	op_privacy_default(&p);
	uint8_t *want = mj_hex(V, "privacy.read_bytes", &n);
	size_t len = op_privacy_encode(&p, buf, sizeof(buf));

	CHECK_MEM(buf, len, want, n);
	free(want);

	uint8_t *er = mj_hex(V, "privacy.erase_bytes", &n);

	CHECK_EQ(op_privacy_apply(&p, &erase, er, n), OP_PARSE_OK);
	CHECK(erase);
	er[n - 1] = 'X';
	CHECK_EQ(op_privacy_apply(&p, &erase, er, n), OP_PARSE_OK);
	CHECK(!erase);
	free(er);
}

static void test_transfer_codec(void)
{
	size_t n;
	struct op_xfer_cmd c;
	uint8_t buf[128];
	uint8_t *start = mj_hex(V, "transfer.start_bytes", &n);

	CHECK_EQ(op_xfer_cmd_parse(&c, start, n), OP_PARSE_OK);
	CHECK_EQ(c.op, OP_XFER_START);
	CHECK_EQ(c.book_id, 0xA1B2C3D4u);
	CHECK_EQ(c.total_len, 70000);
	CHECK_EQ(c.crc32, 0x12345678u);
	CHECK_STR(c.title, "1984");
	free(start);

	struct op_xfer_block b;
	uint8_t *blk = mj_hex(V, "transfer.block.bytes", &n);
	size_t dl;
	uint8_t *data = mj_hex(V, "transfer.block.data", &dl);

	CHECK_EQ(op_xfer_block_parse(&b, blk, n), OP_PARSE_OK);
	CHECK_EQ(b.offset, mj_num(V, "transfer.block.offset"));
	CHECK_EQ(b.crc16, mj_num(V, "transfer.block.crc16"));
	CHECK_MEM(b.data, b.len, data, dl);
	CHECK_EQ(op_crc16(0xFFFF, b.data, b.len), b.crc16);
	free(blk);
	free(data);

	uint8_t *want = mj_hex(V, "transfer.progress_bytes", &n);
	size_t len = op_xfer_progress_encode(0xA1B2C3D4u, OP_XFER_BAD_BLOCK, 4096, buf, sizeof(buf));

	CHECK_MEM(buf, len, want, n);
	free(want);

	struct op_book_info bi = {.book_id = 0xA1B2C3D4u, .size = 70000, .position = 35000};

	strcpy(bi.title, "1984");
	want = mj_hex(V, "transfer.list_reply_bytes", &n);
	len = op_xfer_list_encode(&bi, 1, 0, buf, sizeof(buf));
	CHECK_MEM(buf, len, want, n);
	free(want);
}

static void test_cp1252(void)
{
	size_t n;
	char out[256];
	const char *in = mj_str(V, "cp1252.in");
	uint8_t *want = mj_hex(V, "cp1252.out", &n);
	size_t len = op_utf8_to_cp1252(in, strlen(in), out, sizeof(out));

	CHECK_MEM(out, len, want, n);
	free(want);
}

struct membuf {
	const uint8_t *p;
	size_t n;
};

static int mem_read(void *ctx, uint32_t off, uint8_t *buf, size_t len)
{
	const struct membuf *m = ctx;

	if (off >= m->n) {
		return 0;
	}
	if (len > m->n - off) {
		len = m->n - off;
	}
	memcpy(buf, m->p + off, len);
	return (int)len;
}

static uint8_t fixed_width(void *ctx, uint8_t ch)
{
	(void)ctx;
	(void)ch;
	return 10;
}

static struct opb_info info;

static void test_opb(void)
{
	size_t n;
	uint8_t *book = mj_hex(V, "opb.bytes", &n);
	struct membuf mb = {book, n};

	CHECK_EQ(op_crc32(0, book, n), mj_num(V, "opb.crc32"));
	CHECK_EQ(opb_parse(&info, mem_read, &mb, (uint32_t)n), 0);
	CHECK_STR(info.title, mj_str(V, "opb.title"));
	CHECK_STR(info.author, mj_str(V, "opb.author"));
	CHECK_EQ(info.chapters, 2);
	CHECK_EQ(info.chapter_off[0], mj_num(V, "opb.chapters.0.0"));
	CHECK_EQ(info.chapter_off[1], mj_num(V, "opb.chapters.1.0"));

	size_t tl;
	uint8_t *text = mj_hex(V, "cp1252.out", &tl);

	CHECK_EQ(info.text_len, tl);
	CHECK(memcmp(book + info.text_off, text, tl) == 0);

	/* 20 characters per line, 3 lines per page. */
	struct reader_metrics m = {.width_px = 200, .lines = 3, .width = fixed_width};
	static struct reader rd;

	reader_open(&rd, &info, mem_read, &mb, &m, 0);
	CHECK_EQ(rd.page.nlines, 3);
	CHECK_EQ(rd.page.line[0].len, strlen("Chapter One"));
	/* Second line wraps at a space within 20 characters. */
	CHECK(rd.page.line[1].len <= 20);
	CHECK_EQ(text[rd.page.line[1].start + rd.page.line[1].len], ' ');
	uint32_t p0 = reader_position(&rd);
	int pages = 1;

	while (reader_next(&rd)) {
		pages++;
	}
	CHECK(pages >= 2);
	CHECK_EQ(rd.page.end, info.text_len);
	CHECK_EQ(reader_chapter(&rd), rd.page.start >= info.chapter_off[1] ? 1 : 0);
	/* One line per page: some page starts inside chapter two. */
	struct reader_metrics one = m;
	int saw_ch2 = 0;

	one.lines = 1;
	reader_open(&rd, &info, mem_read, &mb, &one, 0);
	while (reader_next(&rd)) {
		saw_ch2 |= reader_chapter(&rd) == 1;
	}
	CHECK(saw_ch2);
	/* Opened mid-book with no history, back-paging still reaches the start. */
	reader_open(&rd, &info, mem_read, &mb, &m, info.chapter_off[1]);
	CHECK(reader_prev(&rd));
	CHECK(reader_position(&rd) < info.chapter_off[1]);
	reader_open(&rd, &info, mem_read, &mb, &m, 0);
	while (reader_next(&rd)) {
	}
	while (reader_prev(&rd)) {
	}
	CHECK_EQ(reader_position(&rd), p0);
	/* Damaged magic is refused. */
	book[0] = 'X';
	CHECK(opb_parse(&info, mem_read, &mb, (uint32_t)n) != 0);
	free(text);
	free(book);
}

int main(void)
{
	V = mj_load_file(VECTORS_PATH);
	if (!V) {
		fprintf(stderr, "cannot read %s\n", VECTORS_PATH);
		return 2;
	}
	CHECK_EQ(mj_num(V, "protocol_version"), OP_PROTO_VERSION);
	static const struct test_case cases[] = {
		{"crc", test_crc},
		{"status", test_status},
		{"settings", test_settings},
		{"app_filter", test_app_filter},
		{"home_data", test_home_data},
		{"calendar", test_calendar},
		{"data_request", test_data_request},
		{"privacy_codec", test_privacy_codec},
		{"transfer_codec", test_transfer_codec},
		{"cp1252", test_cp1252},
		{"opb_and_reader", test_opb},
	};
	return t_run(cases, (int)(sizeof(cases) / sizeof(cases[0])));
}

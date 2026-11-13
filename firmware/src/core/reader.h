/*
 * OnePage Mini - book reader core: .opb header parsing (protocol section 6)
 * and page layout over Windows-1252 text read through a callback.
 */
#ifndef OP_READER_H
#define OP_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPB_HEADER_LEN 16
#define OPB_MAX_CHAPTERS 512
#define READER_MAX_LINES 48
#define READER_HISTORY 32

struct opb_info {
	uint16_t chapters;
	uint32_t text_off;
	uint32_t text_len;
	char title[65];
	char author[65];
	uint32_t chapter_off[OPB_MAX_CHAPTERS]; /* offsets within the text */
	uint16_t chapters_loaded;
};

/* Reads `len` bytes at `off` of the file. Returns bytes read, < 0 on error. */
typedef int (*reader_read_fn)(void *ctx, uint32_t off, uint8_t *buf, size_t len);

/* Parses the header, metadata and chapter table. 0 on success. */
int opb_parse(struct opb_info *info, reader_read_fn read, void *ctx, uint32_t file_len);

/* Pixel width of one Windows-1252 byte in the reading font. */
typedef uint8_t (*reader_width_fn)(void *ctx, uint8_t ch);

struct reader_metrics {
	uint16_t width_px;
	uint8_t lines;
	reader_width_fn width;
	void *width_ctx;
};

struct reader_line {
	uint32_t start; /* text offset */
	uint16_t len;   /* bytes to draw, trailing space and newline excluded */
};

struct reader_page {
	uint32_t start;
	uint32_t end; /* first offset of the next page */
	uint8_t nlines;
	struct reader_line line[READER_MAX_LINES];
};

/* Text accessor over the file, with a small window cache. */
struct reader_src {
	reader_read_fn read;
	void *ctx;
	uint32_t text_off;
	uint32_t text_len;
	uint8_t cache[512];
	uint32_t cache_at; /* text offset of cache[0] */
	uint16_t cache_len;
};

void reader_src_init(struct reader_src *s, reader_read_fn read, void *ctx, uint32_t text_off,
		     uint32_t text_len);
/* Returns the byte at a text offset, or -1. */
int reader_src_byte(struct reader_src *s, uint32_t at);
/* Copies text bytes for drawing. Returns count. */
size_t reader_src_copy(struct reader_src *s, uint32_t at, char *dst, size_t len);

/* Lays out one page starting at `start`. */
void reader_layout(struct reader_src *s, const struct reader_metrics *m, uint32_t start,
		   struct reader_page *p);

/* Reader session with back-paging. */
struct reader {
	struct reader_src src;
	struct reader_metrics m;
	const struct opb_info *info;
	struct reader_page page;
	uint32_t history[READER_HISTORY];
	uint8_t hist_n;
};

void reader_open(struct reader *r, const struct opb_info *info, reader_read_fn read, void *ctx,
		 const struct reader_metrics *m, uint32_t position);
bool reader_next(struct reader *r);
bool reader_prev(struct reader *r);
/* Byte offset of the current page start: saved as the reading position. */
static inline uint32_t reader_position(const struct reader *r)
{
	return r->page.start;
}
/* Index of the chapter that holds the page start. */
uint16_t reader_chapter(const struct reader *r);

#ifdef __cplusplus
}
#endif

#endif /* OP_READER_H */

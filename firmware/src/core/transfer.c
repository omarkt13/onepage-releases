#include "transfer.h"

#include <string.h>

void xfer_init(struct xfer *x, const struct xfer_io *io)
{
	memset(x, 0, sizeof(*x));
	x->io = io;
}

static void progress(struct xfer_reply *r, uint32_t id, uint8_t status, uint32_t next)
{
	r->len = op_xfer_progress_encode(id, status, next, r->buf, sizeof(r->buf));
}

static void result(struct xfer_reply *r, uint32_t id, uint8_t status)
{
	r->len = op_xfer_result_encode(id, status, r->buf, sizeof(r->buf));
}

static void start(struct xfer *x, const struct op_xfer_cmd *c, struct xfer_reply *r)
{
	if (x->active && x->book_id != c->book_id) {
		progress(r, c->book_id, OP_XFER_BUSY, 0);
		return;
	}
	uint32_t resume = 0;
	uint8_t st = x->io->open(x->io->ctx, c->book_id, c->total_len, c->crc32, c->title,
				 &resume);

	if (st != OP_XFER_OK) {
		x->active = false;
		progress(r, c->book_id, st, 0);
		return;
	}
	x->active = true;
	x->book_id = c->book_id;
	x->total_len = c->total_len;
	x->crc32 = c->crc32;
	x->expected = resume <= c->total_len ? resume : 0;
	x->blocks_since_report = 0;
	x->reject_reported = false;
	progress(r, c->book_id, OP_XFER_OK, x->expected);
}

static void finish(struct xfer *x, uint32_t id, struct xfer_reply *r)
{
	if (!x->active || x->book_id != id) {
		result(r, id, OP_XFER_UNKNOWN_BOOK);
		return;
	}
	if (x->expected != x->total_len) {
		result(r, id, OP_XFER_BAD_BLOCK);
		return;
	}
	uint32_t crc = 0;

	if (x->io->file_crc(x->io->ctx, id, &crc) != 0) {
		x->io->discard(x->io->ctx, id);
		x->active = false;
		result(r, id, OP_XFER_STORAGE);
		return;
	}
	if (crc != x->crc32) {
		x->io->discard(x->io->ctx, id);
		x->active = false;
		result(r, id, OP_XFER_CRC_MISMATCH);
		return;
	}
	x->active = false;
	result(r, id, x->io->commit(x->io->ctx, id) == 0 ? OP_XFER_OK : OP_XFER_STORAGE);
}

void xfer_command(struct xfer *x, const struct op_xfer_cmd *c, struct xfer_reply *r)
{
	r->len = 0;
	switch (c->op) {
	case OP_XFER_START:
		start(x, c, r);
		break;
	case OP_XFER_QUERY:
		if (x->active && x->book_id == c->book_id) {
			x->reject_reported = false;
			progress(r, c->book_id, OP_XFER_OK, x->expected);
		} else {
			progress(r, c->book_id, OP_XFER_UNKNOWN_BOOK, 0);
		}
		break;
	case OP_XFER_CANCEL:
		if (x->active && x->book_id == c->book_id) {
			x->io->discard(x->io->ctx, c->book_id);
			x->active = false;
		}
		result(r, c->book_id, OP_XFER_OK);
		break;
	case OP_XFER_FINISH:
		finish(x, c->book_id, r);
		break;
	default:
		break;
	}
}

void xfer_block(struct xfer *x, const struct op_xfer_block *b, struct xfer_reply *r)
{
	r->len = 0;
	if (!x->active) {
		if (!x->reject_reported) {
			x->reject_reported = true;
			progress(r, 0, OP_XFER_UNKNOWN_BOOK, 0);
		}
		return;
	}
	bool ok = b->offset == x->expected && b->len > 0 &&
		  b->len <= x->total_len - x->expected &&
		  op_crc16(0xFFFF, b->data, b->len) == b->crc16;

	if (!ok) {
		if (!x->reject_reported) {
			x->reject_reported = true;
			x->blocks_since_report = 0;
			progress(r, x->book_id, OP_XFER_BAD_BLOCK, x->expected);
		}
		return;
	}
	if (x->io->write(x->io->ctx, x->book_id, b->offset, b->data, b->len) != 0) {
		x->io->discard(x->io->ctx, x->book_id);
		x->active = false;
		progress(r, x->book_id, OP_XFER_STORAGE, 0);
		return;
	}
	x->expected += (uint32_t)b->len;
	x->reject_reported = false;
	x->blocks_since_report++;
	if (x->blocks_since_report >= XFER_PROGRESS_EVERY || x->expected == x->total_len) {
		x->blocks_since_report = 0;
		progress(r, x->book_id, OP_XFER_OK, x->expected);
	}
}

void xfer_suspend(struct xfer *x)
{
	/* The partial file stays; START with the same id resumes from its length. */
	x->active = false;
	x->reject_reported = true;
}

/*
 * OnePage Mini - book transfer reassembly (protocol 5.8, 5.9).
 * Storage is behind struct xfer_io so this file has no file-system code.
 */
#ifndef OP_TRANSFER_H
#define OP_TRANSFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "op_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XFER_PROGRESS_EVERY 32u

struct xfer_io {
	void *ctx;
	/*
	 * Opens a partial book for writing. If a partial file with the same id,
	 * length and CRC exists, sets *resume_at to its length. Returns an
	 * op_xfer_status.
	 */
	uint8_t (*open)(void *ctx, uint32_t book_id, uint32_t total_len, uint32_t crc32,
			const char *title, uint32_t *resume_at);
	/* Appends at `offset`. 0 on success. */
	int (*write)(void *ctx, uint32_t book_id, uint32_t offset, const uint8_t *data,
		     size_t len);
	/* Computes the CRC-32 of the stored partial file. 0 on success. */
	int (*file_crc)(void *ctx, uint32_t book_id, uint32_t *crc32);
	/* Makes the partial file a library book. 0 on success. */
	int (*commit)(void *ctx, uint32_t book_id);
	/* Deletes the partial file. */
	void (*discard)(void *ctx, uint32_t book_id);
};

struct xfer {
	const struct xfer_io *io;
	bool active;
	uint32_t book_id;
	uint32_t total_len;
	uint32_t crc32;
	uint32_t expected;
	uint32_t blocks_since_report;
	bool reject_reported;
};

/* A reply to notify on Transfer control, if len != 0. */
struct xfer_reply {
	uint8_t buf[16];
	size_t len;
};

void xfer_init(struct xfer *x, const struct xfer_io *io);
/* Handles START/QUERY/CANCEL/FINISH. DELETE, LIST and FAST belong to the caller. */
void xfer_command(struct xfer *x, const struct op_xfer_cmd *c, struct xfer_reply *r);
void xfer_block(struct xfer *x, const struct op_xfer_block *b, struct xfer_reply *r);
/* The link dropped: keep the partial file for resume, stop accepting blocks. */
void xfer_suspend(struct xfer *x);
/*
 * DELETE of a book: if it is the book now being received, discard the
 * partial file and stop the transfer. Returns true if it did that.
 */
bool xfer_forget(struct xfer *x, uint32_t book_id);

#ifdef __cplusplus
}
#endif

#endif /* OP_TRANSFER_H */

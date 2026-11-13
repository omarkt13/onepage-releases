/*
 * OnePage Mini - book library on the external NOR flash (littlefs).
 * Files: /lfs/b/<id>.opb (book), <id>.prt (partial), <id>.inf (metadata).
 * The external flash holds only books (spec section 7).
 */
#ifndef OP_BOOKS_H
#define OP_BOOKS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/op_proto.h"
#include "core/transfer.h"

#define BOOKS_MAX 64

int books_init(void);
bool books_ready(void);
uint16_t books_count(void);
/* Library order: as stored. Fills position from the settings store. */
bool books_get(uint16_t index, struct op_book_info *out);
int books_delete(uint32_t book_id);
int books_erase_all(void);
void books_storage(uint32_t *free_kib, uint32_t *total_kib);
const struct xfer_io *books_xfer_io(void);

/* Reader access. One book open at a time. */
int books_open(uint32_t book_id, uint32_t *size);
int books_read(void *ctx, uint32_t off, uint8_t *buf, size_t len);
void books_close(void);

#endif

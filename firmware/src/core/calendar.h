/*
 * OnePage Mini - calendar store. Holds the synced week (protocol 5.5).
 * A new blob is staged and replaces the current one only after a complete,
 * checked transfer. Persistence is the caller's job (see cal_store_blob()).
 */
#ifndef OP_CALENDAR_H
#define OP_CALENDAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAL_MAX_BLOB 16384u
#define CAL_MAX_EVENTS 100u
#define CAL_TITLE_MAX_BYTES 160u
#define CAL_PLACE_MAX_BYTES 120u
#define CAL_FLAG_ALL_DAY 0x01

struct cal_event {
	uint32_t start;
	uint32_t end;
	bool all_day;
	const char *title; /* not terminated */
	uint8_t title_len;
	const char *place;
	uint8_t place_len;
};

struct cal_store {
	/* Current calendar. */
	uint8_t blob[CAL_MAX_BLOB];
	uint32_t blob_len;
	uint32_t version;
	uint32_t generated_at;
	uint16_t count;
	uint16_t idx[CAL_MAX_EVENTS];

	/* Transfer in progress. */
	uint8_t stage[CAL_MAX_BLOB];
	bool staging;
	uint32_t stage_version;
	uint32_t stage_len;
	uint32_t stage_crc;
	uint32_t stage_received;
};

/* Result of a sync step: protocol status plus the offset to report. */
struct cal_result {
	uint8_t status;
	uint32_t version;
	uint32_t next_offset;
	bool changed; /* the current calendar changed; persist it */
};

void cal_init(struct cal_store *c);
/* Checks a blob. On success fills idx/count if non-NULL. */
bool cal_validate(const uint8_t *blob, uint32_t len, uint16_t *idx, uint16_t *count,
		  uint32_t *generated_at);
/* Loads a persisted blob (at boot). False if it is not valid. */
bool cal_load(struct cal_store *c, const uint8_t *blob, uint32_t len, uint32_t version);
void cal_erase(struct cal_store *c);

struct cal_result cal_begin(struct cal_store *c, uint32_t version, uint32_t total_len,
			    uint32_t crc32);
/* Returns status OK (no reply needed) or OUT_OF_ORDER/NO_BEGIN (reply). */
struct cal_result cal_data(struct cal_store *c, uint32_t offset, const uint8_t *data,
			   size_t len);
struct cal_result cal_commit(struct cal_store *c, uint32_t version);
struct cal_result cal_clear(struct cal_store *c);

static inline uint16_t cal_count(const struct cal_store *c)
{
	return c->count;
}
bool cal_get(const struct cal_store *c, uint16_t i, struct cal_event *e);

/* Index of the first event that has not ended at `now`, or count if none. */
uint16_t cal_first_current(const struct cal_store *c, uint32_t now);
/* Number of events that have not ended at `now`. */
uint16_t cal_current_count(const struct cal_store *c, uint32_t now);
/* The next event (in progress or upcoming, not all-day first) for Home. */
bool cal_next_event(const struct cal_store *c, uint32_t now, struct cal_event *e);

#ifdef __cplusplus
}
#endif

#endif /* OP_CALENDAR_H */

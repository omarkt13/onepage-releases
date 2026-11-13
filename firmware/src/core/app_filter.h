/*
 * OnePage Mini - per-app filter (protocol 5.3). Up to 64 entries, oldest
 * first. A new app is added as SHOW. When full, the oldest entry that is
 * still SHOW with no flags and pattern 0 is dropped.
 */
#ifndef OP_APP_FILTER_H
#define OP_APP_FILTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "op_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_FILTER_MAX 64

struct app_filter {
	struct op_filter_entry e[APP_FILTER_MAX];
	uint16_t n;
	bool dirty; /* changed since last persisted */
};

void app_filter_init(struct app_filter *f);
const struct op_filter_entry *app_filter_find(const struct app_filter *f, const char *id);
/*
 * Records that an app sent a notification. Returns its entry. Never NULL: if
 * the list is full of customised entries, returns a static SHOW entry and does
 * not store the app.
 */
const struct op_filter_entry *app_filter_seen(struct app_filter *f, const char *id);
/* SET: updates or adds. Returns 0 or OP_ERR_STORAGE_FULL low byte. */
uint8_t app_filter_set(struct app_filter *f, const struct op_filter_entry *e);
uint8_t app_filter_remove(struct app_filter *f, const char *id);
/* Builds a LIST reply into buf; as many entries as fit. */
size_t app_filter_list_reply(const struct app_filter *f, uint16_t start, uint8_t *buf,
			     size_t cap);
/* Handles a whole write to the App filter characteristic; fills the reply. */
size_t app_filter_handle(struct app_filter *f, const uint8_t *req, size_t req_len, uint8_t *rsp,
			 size_t rsp_cap);

#ifdef __cplusplus
}
#endif

#endif /* OP_APP_FILTER_H */

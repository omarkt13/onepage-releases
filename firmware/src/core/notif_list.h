/*
 * OnePage Mini - notification list. RAM only, never written to storage
 * (spec section 7). 50 items, 512 characters of text each, oldest dropped.
 * Text is held in Windows-1252, ready to draw.
 */
#ifndef OP_NOTIF_LIST_H
#define OP_NOTIF_LIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NOTIF_MAX 50
#define NOTIF_APP_ID_MAX 64
#define NOTIF_APP_NAME_MAX 32
#define NOTIF_TITLE_MAX 96
#define NOTIF_SUBTITLE_MAX 64
#define NOTIF_MESSAGE_MAX 512

/* ANCS category IDs that change behaviour. */
#define NOTIF_CAT_INCOMING_CALL 1
#define NOTIF_CAT_MISSED_CALL 2

struct notif {
	uint32_t uid;
	uint32_t received_at; /* local seconds */
	uint8_t category;
	bool silent;      /* no LED or vibration (filter or iOS flag) */
	bool title_only;  /* hide text, show app and time */
	bool has_negative; /* iOS offers a negative action (dismiss, decline) */
	bool has_positive;
	bool read;
	char app_id[NOTIF_APP_ID_MAX + 1];
	char app_name[NOTIF_APP_NAME_MAX + 1];
	char title[NOTIF_TITLE_MAX + 1];
	char subtitle[NOTIF_SUBTITLE_MAX + 1];
	char message[NOTIF_MESSAGE_MAX + 1];
};

struct notif_list {
	struct notif items[NOTIF_MAX];
	/* Ring order: order[0] is the oldest slot index. */
	uint8_t order[NOTIF_MAX];
	uint8_t n;
};

void notif_list_init(struct notif_list *l);
/* Erases every notification (phone away, erase command). */
void notif_list_clear(struct notif_list *l);
/* Adds a new notification or replaces one with the same UID. Returns the slot. */
struct notif *notif_list_put(struct notif_list *l, const struct notif *n);
bool notif_list_remove(struct notif_list *l, uint32_t uid);
struct notif *notif_list_find(struct notif_list *l, uint32_t uid);
static inline uint8_t notif_list_count(const struct notif_list *l)
{
	return l->n;
}
/* i = 0 is the newest. */
struct notif *notif_list_newest(struct notif_list *l, uint8_t i);
uint8_t notif_list_unread(const struct notif_list *l);

/*
 * Builds a notification from ANCS attributes, which arrive one at a time
 * for one UID. UTF-8 is converted on the way in.
 */
enum notif_attr {
	NOTIF_ATTR_APP_ID = 0,
	NOTIF_ATTR_TITLE = 1,
	NOTIF_ATTR_SUBTITLE = 2,
	NOTIF_ATTR_MESSAGE = 3,
	NOTIF_ATTR_DATE = 5,
};

struct notif_builder {
	struct notif n;
	bool active;
};

void notif_builder_begin(struct notif_builder *b, uint32_t uid, uint8_t category, bool silent,
			 bool has_positive, bool has_negative, uint32_t now);
/* Returns true when this attribute was the last requested one (`last_attr`). */
bool notif_builder_attr(struct notif_builder *b, uint32_t uid, uint8_t attr_id,
			const uint8_t *data, size_t len, uint8_t last_attr);

#ifdef __cplusplus
}
#endif

#endif /* OP_NOTIF_LIST_H */

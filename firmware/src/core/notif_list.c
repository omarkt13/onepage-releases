#include "notif_list.h"

#include <string.h>

#include "op_util.h"
#include "text.h"

void notif_list_init(struct notif_list *l)
{
	memset(l, 0, sizeof(*l));
}

void notif_list_clear(struct notif_list *l)
{
	/* Overwrite, so no text stays in RAM. */
	memset(l, 0, sizeof(*l));
}

static int find_pos(const struct notif_list *l, uint32_t uid)
{
	for (int i = 0; i < l->n; i++) {
		if (l->items[l->order[i]].uid == uid) {
			return i;
		}
	}
	return -1;
}

static uint8_t free_slot(const struct notif_list *l)
{
	bool used[NOTIF_MAX] = {false};

	for (int i = 0; i < l->n; i++) {
		used[l->order[i]] = true;
	}
	for (uint8_t s = 0; s < NOTIF_MAX; s++) {
		if (!used[s]) {
			return s;
		}
	}
	return 0;
}

struct notif *notif_list_put(struct notif_list *l, const struct notif *n)
{
	int pos = find_pos(l, n->uid);
	uint8_t slot;

	if (pos >= 0) {
		/* Modified notification: replace in place and move to newest. */
		slot = l->order[pos];
		memmove(&l->order[pos], &l->order[pos + 1], (size_t)(l->n - pos - 1));
		l->order[l->n - 1] = slot;
	} else {
		if (l->n == NOTIF_MAX) {
			slot = l->order[0];
			memmove(&l->order[0], &l->order[1], NOTIF_MAX - 1);
			l->n--;
		} else {
			slot = free_slot(l);
		}
		l->order[l->n++] = slot;
	}
	l->items[slot] = *n;
	return &l->items[slot];
}

bool notif_list_remove(struct notif_list *l, uint32_t uid)
{
	int pos = find_pos(l, uid);

	if (pos < 0) {
		return false;
	}
	memset(&l->items[l->order[pos]], 0, sizeof(struct notif));
	memmove(&l->order[pos], &l->order[pos + 1], (size_t)(l->n - pos - 1));
	l->n--;
	return true;
}

struct notif *notif_list_find(struct notif_list *l, uint32_t uid)
{
	int pos = find_pos(l, uid);

	return pos < 0 ? NULL : &l->items[l->order[pos]];
}

struct notif *notif_list_newest(struct notif_list *l, uint8_t i)
{
	if (i >= l->n) {
		return NULL;
	}
	return &l->items[l->order[l->n - 1 - i]];
}

uint8_t notif_list_unread(const struct notif_list *l)
{
	uint8_t c = 0;

	for (int i = 0; i < l->n; i++) {
		if (!l->items[l->order[i]].read) {
			c++;
		}
	}
	return c;
}

void notif_builder_begin(struct notif_builder *b, uint32_t uid, uint8_t category, bool silent,
			 bool has_positive, bool has_negative, uint32_t now)
{
	memset(b, 0, sizeof(*b));
	b->active = true;
	b->n.uid = uid;
	b->n.category = category;
	b->n.silent = silent;
	b->n.has_positive = has_positive;
	b->n.has_negative = has_negative;
	b->n.received_at = now;
}

bool notif_builder_attr(struct notif_builder *b, uint32_t uid, uint8_t attr_id,
			const uint8_t *data, size_t len, uint8_t last_attr)
{
	if (!b->active || uid != b->n.uid) {
		return false;
	}
	const char *s = (const char *)data;

	switch (attr_id) {
	case NOTIF_ATTR_APP_ID:
		op_strlcpy_utf8(b->n.app_id, sizeof(b->n.app_id), s, len);
		break;
	case NOTIF_ATTR_TITLE:
		op_utf8_to_cp1252(s, len, b->n.title, sizeof(b->n.title));
		break;
	case NOTIF_ATTR_SUBTITLE:
		op_utf8_to_cp1252(s, len, b->n.subtitle, sizeof(b->n.subtitle));
		break;
	case NOTIF_ATTR_MESSAGE:
		op_utf8_to_cp1252(s, len, b->n.message, sizeof(b->n.message));
		break;
	default:
		break;
	}
	if (attr_id == last_attr) {
		b->active = false;
		return true;
	}
	return false;
}

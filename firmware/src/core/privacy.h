/*
 * OnePage Mini - phone-away rules (firmware spec section 7).
 * Time base: milliseconds of uptime, which keeps counting while asleep.
 */
#ifndef OP_PRIVACY_H
#define OP_PRIVACY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PRIV_LOCK_DELAY_MS (2u * 60u * 1000u)
#define PRIV_ERASE_DELAY_MS (7ull * 24u * 3600u * 1000u)

enum priv_action {
	PRIV_ACT_NONE = 0,
	/* Redraw to locked Home; erase notifications and music details. */
	PRIV_ACT_LOCK = 1u << 0,
	/* Link is back: leave locked Home. */
	PRIV_ACT_UNLOCK = 1u << 1,
	/* Erase calendar and weather. */
	PRIV_ACT_ERASE_CAL = 1u << 2,
};

struct privacy {
	bool link;
	bool locked;
	bool cal_erased;
	uint64_t down_since_ms;
	/* Time away counted before the last restart (restored from storage). */
	uint64_t away_before_ms;
};

/* At boot the device has no link, holds nothing sensitive, and starts locked. */
void privacy_init(struct privacy *p, uint64_t now_ms);
unsigned privacy_link_up(struct privacy *p, uint64_t now_ms);
void privacy_link_down(struct privacy *p, uint64_t now_ms);
unsigned privacy_tick(struct privacy *p, uint64_t now_ms);
/* Adds time away that was counted before a restart. Call after init. */
void privacy_set_away_before(struct privacy *p, uint64_t away_ms);
/* Total time without the phone, including time before a restart. 0 when linked. */
uint64_t privacy_away_ms(const struct privacy *p, uint64_t now_ms);
/* Milliseconds until privacy_tick() has work, or UINT64_MAX. */
uint64_t privacy_next_deadline(const struct privacy *p, uint64_t now_ms);

static inline bool privacy_locked(const struct privacy *p)
{
	return p->locked;
}

/* Whether the Calendar screen is available now. */
static inline bool privacy_calendar_visible(const struct privacy *p, bool show_when_away)
{
	return !p->locked || show_when_away;
}

#ifdef __cplusplus
}
#endif

#endif /* OP_PRIVACY_H */

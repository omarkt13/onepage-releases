#include "privacy.h"

void privacy_init(struct privacy *p, uint64_t now_ms)
{
	p->link = false;
	p->locked = true;
	p->cal_erased = false;
	p->down_since_ms = now_ms;
	p->away_before_ms = 0;
}

void privacy_set_away_before(struct privacy *p, uint64_t away_ms)
{
	if (!p->link) {
		p->away_before_ms = away_ms;
	}
}

uint64_t privacy_away_ms(const struct privacy *p, uint64_t now_ms)
{
	if (p->link) {
		return 0;
	}
	return now_ms - p->down_since_ms + p->away_before_ms;
}

unsigned privacy_link_up(struct privacy *p, uint64_t now_ms)
{
	unsigned act = PRIV_ACT_NONE;

	(void)now_ms;
	p->link = true;
	p->cal_erased = false;
	p->away_before_ms = 0;
	if (p->locked) {
		p->locked = false;
		act |= PRIV_ACT_UNLOCK;
	}
	return act;
}

void privacy_link_down(struct privacy *p, uint64_t now_ms)
{
	if (p->link) {
		p->link = false;
		p->down_since_ms = now_ms;
		p->away_before_ms = 0;
	}
}

unsigned privacy_tick(struct privacy *p, uint64_t now_ms)
{
	unsigned act = PRIV_ACT_NONE;

	if (p->link) {
		return act;
	}
	uint64_t away = privacy_away_ms(p, now_ms);

	if (!p->locked && away >= PRIV_LOCK_DELAY_MS) {
		p->locked = true;
		act |= PRIV_ACT_LOCK;
	}
	if (!p->cal_erased && away >= PRIV_ERASE_DELAY_MS) {
		p->cal_erased = true;
		act |= PRIV_ACT_ERASE_CAL;
	}
	return act;
}

uint64_t privacy_next_deadline(const struct privacy *p, uint64_t now_ms)
{
	if (p->link) {
		return UINT64_MAX;
	}
	uint64_t away = privacy_away_ms(p, now_ms);

	if (!p->locked) {
		return away >= PRIV_LOCK_DELAY_MS ? 0 : PRIV_LOCK_DELAY_MS - away;
	}
	if (!p->cal_erased) {
		return away >= PRIV_ERASE_DELAY_MS ? 0 : PRIV_ERASE_DELAY_MS - away;
	}
	return UINT64_MAX;
}

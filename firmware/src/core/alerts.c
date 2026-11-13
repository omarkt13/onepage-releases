#include "alerts.h"

#include <string.h>

#include "notif_list.h"

bool quiet_active(const struct op_settings *s, uint16_t m)
{
	if (!s->quiet_on || s->quiet_start == s->quiet_end) {
		return false;
	}
	if (s->quiet_start < s->quiet_end) {
		return m >= s->quiet_start && m < s->quiet_end;
	}
	return m >= s->quiet_start || m < s->quiet_end;
}

uint8_t alert_pattern(const struct op_settings *s, uint16_t minute_of_day,
		      const struct alert_input *in)
{
	if (in->locked || in->pre_existing || in->ios_silent || in->filter_mode != OP_MODE_SHOW ||
	    s->alert_strength == 0 || in->filter_pattern == OP_PATTERN_NONE ||
	    quiet_active(s, minute_of_day)) {
		return OP_PATTERN_NONE;
	}
	if (in->filter_pattern != OP_PATTERN_DEFAULT) {
		return in->filter_pattern;
	}
	return in->category == NOTIF_CAT_INCOMING_CALL ? OP_PATTERN_LONG : OP_PATTERN_SINGLE;
}

void sched_init(struct sched *s)
{
	memset(s, 0, sizeof(*s));
}

static struct sched_req req(uint8_t kind, uint8_t reason)
{
	struct sched_req r = {kind, reason};

	return r;
}

struct sched_req sched_connected(struct sched *s, uint64_t up_s, uint32_t local_now)
{
	s->connected = true;
	s->last_weather_s = up_s;
	s->last_cal_slot = local_now / SCHED_CAL_SLOT_S;
	s->cal_slot_valid = local_now != 0;
	return req(OP_REQ_BOTH, OP_REASON_RECONNECT);
}

void sched_disconnected(struct sched *s)
{
	s->connected = false;
}

struct sched_req sched_tick(struct sched *s, uint64_t up_s, bool clock_valid, uint32_t local_now,
			    uint32_t weather_valid_until)
{
	uint8_t kind = 0;
	uint8_t reason = OP_REASON_SCHEDULE;

	if (!s->connected) {
		return req(0, 0);
	}
	uint64_t since = up_s - s->last_weather_s;

	if (since >= SCHED_WEATHER_PERIOD_S) {
		kind |= OP_REQ_WEATHER;
	} else if (clock_valid && weather_valid_until != 0 && local_now >= weather_valid_until &&
		   since >= SCHED_EXPIRED_RETRY_S) {
		kind |= OP_REQ_WEATHER;
		reason = OP_REASON_EXPIRED;
	}
	if (clock_valid) {
		uint32_t slot = local_now / SCHED_CAL_SLOT_S;

		if (!s->cal_slot_valid) {
			s->cal_slot_valid = true;
			s->last_cal_slot = slot;
		} else if (slot != s->last_cal_slot) {
			s->last_cal_slot = slot;
			kind |= OP_REQ_CALENDAR;
		}
	}
	if (kind & OP_REQ_WEATHER) {
		s->last_weather_s = up_s;
	}
	return req(kind, kind ? reason : 0);
}

uint32_t sched_next_s(const struct sched *s, uint64_t up_s, bool clock_valid, uint32_t local_now)
{
	if (!s->connected) {
		return UINT32_MAX;
	}
	uint64_t since = up_s - s->last_weather_s;
	uint32_t next = since >= SCHED_WEATHER_PERIOD_S ? 0
							 : (uint32_t)(SCHED_WEATHER_PERIOD_S - since);

	if (clock_valid) {
		uint32_t to_slot = SCHED_CAL_SLOT_S - (local_now % SCHED_CAL_SLOT_S);

		if (to_slot < next) {
			next = to_slot;
		}
	}
	/* Also wake for weather expiry checks. */
	return next < SCHED_EXPIRED_RETRY_S ? next : SCHED_EXPIRED_RETRY_S;
}

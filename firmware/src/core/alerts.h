/*
 * OnePage Mini - alert decisions (quiet hours, per-app pattern) and the
 * schedule of data requests to the app (protocol 5.6 and 8).
 */
#ifndef OP_ALERTS_H
#define OP_ALERTS_H

#include <stdbool.h>
#include <stdint.h>

#include "op_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

bool quiet_active(const struct op_settings *s, uint16_t minute_of_day);

struct alert_input {
	uint8_t filter_mode;    /* op_filter_mode */
	uint8_t filter_pattern; /* op_alert_pattern */
	uint8_t category;       /* ANCS category */
	bool ios_silent;
	bool pre_existing;
	bool locked;            /* phone away: no LED or vibration */
};

/* Returns the pattern to play, or OP_PATTERN_NONE. Never OP_PATTERN_DEFAULT. */
uint8_t alert_pattern(const struct op_settings *s, uint16_t minute_of_day,
		      const struct alert_input *in);

/* ---- data request schedule ---------------------------------------------- */

#define SCHED_WEATHER_PERIOD_S 3600u
#define SCHED_CAL_SLOT_S (6u * 3600u)
#define SCHED_EXPIRED_RETRY_S 600u

struct sched {
	bool connected;
	uint64_t last_weather_s; /* uptime seconds */
	uint32_t last_cal_slot;  /* local seconds / 6 h */
	bool cal_slot_valid;
};

struct sched_req {
	uint8_t kind;   /* 0 = nothing, else op_req_kind */
	uint8_t reason; /* op_req_reason */
};

void sched_init(struct sched *s);
/* At connection (after the clock is set). Always asks for both. */
struct sched_req sched_connected(struct sched *s, uint64_t up_s, uint32_t local_now);
void sched_disconnected(struct sched *s);
/*
 * Periodic check. `clock_valid` false skips calendar slots. weather_valid_until
 * is 0 when there is no weather.
 */
struct sched_req sched_tick(struct sched *s, uint64_t up_s, bool clock_valid, uint32_t local_now,
			    uint32_t weather_valid_until);
/* Seconds until sched_tick() may have work. */
uint32_t sched_next_s(const struct sched *s, uint64_t up_s, bool clock_valid,
		      uint32_t local_now);

#ifdef __cplusplus
}
#endif

#endif /* OP_ALERTS_H */

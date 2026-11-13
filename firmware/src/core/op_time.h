/*
 * OnePage Mini - local wall-clock time. "Local seconds" count from
 * 2000-01-01 00:00:00 local time (protocol section 1). No time zones.
 */
#ifndef OP_TIME_H
#define OP_TIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OP_SECS_PER_DAY 86400u

struct op_civil {
	uint16_t year;
	uint8_t month;   /* 1..12 */
	uint8_t day;     /* 1..31 */
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	uint8_t weekday; /* 0 = Monday .. 6 = Sunday */
};

uint32_t op_time_from_civil(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
			    uint8_t minute, uint8_t second);
void op_time_to_civil(uint32_t t, struct op_civil *c);

static inline uint32_t op_time_day(uint32_t t)
{
	return t / OP_SECS_PER_DAY;
}

static inline uint16_t op_time_minute_of_day(uint32_t t)
{
	return (uint16_t)((t % OP_SECS_PER_DAY) / 60u);
}

/* "14:05" or "2:05 pm". Returns length. */
size_t op_fmt_clock(uint32_t t, bool clock_12h, char *buf, size_t cap);
/* "Fri 13 Nov". */
size_t op_fmt_date(uint32_t t, char *buf, size_t cap);
/* "Friday 13 November". */
size_t op_fmt_date_long(uint32_t t, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* OP_TIME_H */

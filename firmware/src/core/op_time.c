#include "op_time.h"

#include <stdio.h>

/* Days from 1970-01-01 to y-m-d (Howard Hinnant's algorithm). */
static int32_t days_from_civil(int32_t y, uint32_t m, uint32_t d)
{
	y -= m <= 2;
	const int32_t era = (y >= 0 ? y : y - 399) / 400;
	const uint32_t yoe = (uint32_t)(y - era * 400);
	const uint32_t doy = (153 * (m + (m > 2 ? (uint32_t)-3 : 9)) + 2) / 5 + d - 1;
	const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

	return era * 146097 + (int32_t)doe - 719468;
}

#define DAYS_1970_TO_2000 10957

uint32_t op_time_from_civil(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
			    uint8_t minute, uint8_t second)
{
	int32_t days = days_from_civil(year, month, day) - DAYS_1970_TO_2000;

	if (days < 0) {
		return 0;
	}
	return (uint32_t)days * OP_SECS_PER_DAY + hour * 3600u + minute * 60u + second;
}

void op_time_to_civil(uint32_t t, struct op_civil *c)
{
	uint32_t days = t / OP_SECS_PER_DAY;
	uint32_t rem = t % OP_SECS_PER_DAY;
	int32_t z = (int32_t)days + DAYS_1970_TO_2000 + 719468;
	const int32_t era = z / 146097;
	const uint32_t doe = (uint32_t)(z - era * 146097);
	const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const uint32_t mp = (5 * doy + 2) / 153;
	const uint32_t d = doy - (153 * mp + 2) / 5 + 1;
	const uint32_t m = mp < 10 ? mp + 3 : mp - 9;

	c->year = (uint16_t)((int32_t)yoe + era * 400 + (m <= 2));
	c->month = (uint8_t)m;
	c->day = (uint8_t)d;
	c->hour = (uint8_t)(rem / 3600);
	c->minute = (uint8_t)((rem / 60) % 60);
	c->second = (uint8_t)(rem % 60);
	/* 2000-01-01 was a Saturday (5 when Monday is 0). */
	c->weekday = (uint8_t)((days + 5) % 7);
}

static const char *const wd_short[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
static const char *const wd_long[] = {"Monday", "Tuesday", "Wednesday", "Thursday",
				      "Friday", "Saturday", "Sunday"};
static const char *const mon_short[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
					"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const char *const mon_long[] = {"January", "February", "March",     "April",
				       "May",     "June",     "July",      "August",
				       "September", "October", "November", "December"};

static size_t clamp_len(int n, size_t cap)
{
	if (n < 0) {
		return 0;
	}
	return (size_t)n < cap ? (size_t)n : (cap ? cap - 1 : 0);
}

size_t op_fmt_clock(uint32_t t, bool clock_12h, char *buf, size_t cap)
{
	struct op_civil c;

	op_time_to_civil(t, &c);
	if (clock_12h) {
		unsigned h = c.hour % 12 ? c.hour % 12 : 12;

		return clamp_len(snprintf(buf, cap, "%u:%02u %s", h, c.minute,
					  c.hour < 12 ? "am" : "pm"), cap);
	}
	return clamp_len(snprintf(buf, cap, "%02u:%02u", c.hour, c.minute), cap);
}

size_t op_fmt_date(uint32_t t, char *buf, size_t cap)
{
	struct op_civil c;

	op_time_to_civil(t, &c);
	return clamp_len(snprintf(buf, cap, "%s %u %s", wd_short[c.weekday], c.day,
				  mon_short[c.month - 1]), cap);
}

size_t op_fmt_date_long(uint32_t t, char *buf, size_t cap)
{
	struct op_civil c;

	op_time_to_civil(t, &c);
	return clamp_len(snprintf(buf, cap, "%s %u %s", wd_long[c.weekday], c.day,
				  mon_long[c.month - 1]), cap);
}

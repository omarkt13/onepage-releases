#include "battery_policy.h"

void batt_policy_init(struct batt_policy *b)
{
	b->warned = false;
	b->low = false;
	b->low_since_s = 0;
}

unsigned batt_policy_reading(struct batt_policy *b, uint32_t now_s, uint8_t percent,
			     uint16_t millivolts, uint8_t charging)
{
	unsigned act = BATT_ACT_NONE;
	bool on_battery = charging == 0;
	bool mv_known = millivolts != 0;
	bool low = on_battery &&
		   ((percent <= BATT_OFF_PERCENT && (!mv_known || millivolts <= BATT_OFF_MV)) ||
		    (mv_known && millivolts <= BATT_FLOOR_MV));

	if (low) {
		if (!b->low) {
			b->low = true;
			b->low_since_s = now_s;
		} else if (now_s - b->low_since_s >= BATT_CONFIRM_S) {
			act |= BATT_ACT_OFF;
		}
	} else {
		b->low = false;
	}
	if (on_battery && percent <= BATT_WARN_PERCENT && !b->warned) {
		b->warned = true;
		act |= BATT_ACT_WARN;
	}
	if (percent > BATT_WARN_PERCENT + BATT_WARN_HYST || !on_battery) {
		b->warned = false;
	}
	return act;
}

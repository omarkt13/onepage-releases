/*
 * OnePage Mini - low-battery decisions (firmware spec section 9: warn at
 * 10 %, at 3 % show "charge me" and switch off).
 *
 * A single gauge reading never switches the device off. The gauge model for
 * the 4.35 V cell is not final, and one wrong reading (for example right
 * after the gauge starts) would otherwise leave the device off until the
 * charger is connected. Off needs low readings in a row that span at least
 * BATT_CONFIRM_S, and "low" needs the cell voltage to agree with the
 * percentage when the voltage is known.
 */
#ifndef OP_BATTERY_POLICY_H
#define OP_BATTERY_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BATT_WARN_PERCENT 10
#define BATT_OFF_PERCENT 3
/* At or below 3 %, switch off only if the cell is also at or below this. */
#define BATT_OFF_MV 3550
/* Switch off at or below this voltage, whatever the percentage says. */
#define BATT_FLOOR_MV 3300
/* Time the low state must last before the device switches off. */
#define BATT_CONFIRM_S 60u
/* The warning comes back only after the level rises this much above 10 %. */
#define BATT_WARN_HYST 5

enum batt_act {
	BATT_ACT_NONE = 0,
	BATT_ACT_WARN = 1, /* play the low-battery alert once */
	BATT_ACT_OFF = 2,  /* show "charge me" and switch off */
};

struct batt_policy {
	bool warned;
	bool low;              /* the last reading met the off rule */
	uint32_t low_since_s;  /* uptime of the first reading in this low run */
};

void batt_policy_init(struct batt_policy *b);

/*
 * One gauge reading. charging is enum op_charge_state: only OP_CHG_NONE (0)
 * counts as running on the battery. millivolts 0 means "not known".
 * now_s is uptime in seconds.
 */
unsigned batt_policy_reading(struct batt_policy *b, uint32_t now_s, uint8_t percent,
			     uint16_t millivolts, uint8_t charging);

#ifdef __cplusplus
}
#endif

#endif

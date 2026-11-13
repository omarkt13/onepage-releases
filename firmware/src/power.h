/* OnePage Mini - power manager: shipping/off state and battery rules. */
#ifndef OP_POWER_H
#define OP_POWER_H

#include <stdint.h>

#include "core/battery_policy.h"

/* The rules themselves are in core/battery_policy.c. */
#define BATTERY_WARN_PERCENT BATT_WARN_PERCENT
#define BATTERY_OFF_PERCENT BATT_OFF_PERCENT

/*
 * Shows a last message, cuts the display supply and enters System OFF
 * (under 2 uA). The power button or the charger wakes the device, which
 * then boots from the start. Does not return.
 */
void power_off(const char *title, const char *body);

#endif

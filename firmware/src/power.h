/* OnePage Mini - power manager: shipping/off state and battery rules. */
#ifndef OP_POWER_H
#define OP_POWER_H

#include <stdint.h>

#define BATTERY_WARN_PERCENT 10
#define BATTERY_OFF_PERCENT 3

/*
 * Shows a last message, cuts the display supply and enters System OFF
 * (under 2 uA). The power button or the charger wakes the device, which
 * then boots from the start. Does not return.
 */
void power_off(const char *title, const char *body);

#endif

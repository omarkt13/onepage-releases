/* OnePage Mini - battery, charger, LED and vibration. */
#ifndef OP_POWER_HW_H
#define OP_POWER_HW_H

#include <stdbool.h>
#include <stdint.h>

/* MCP73833 status pins. Posts EVT_CHARGER on change. */
int charger_init(void);
uint8_t charger_state(void); /* enum op_charging */
void charger_prepare_poweroff(void);

/* MAX17048 fuel gauge. */
int battery_init(void);
/* Reads the gauge into g_app.battery. Returns 0 on success. */
int battery_update(void);

/* LED and haptic patterns (enum op_alert_pattern, never DEFAULT). */
int alerts_hw_init(void);
void alerts_play(uint8_t pattern, uint8_t strength);
void alerts_stop(void);

#endif

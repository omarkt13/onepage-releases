/*
 * OnePage Mini - power manager.
 *
 * States (firmware spec section 8):
 *   Off        System OFF; wake by power button or charger.     power_off()
 *   Connected  Zephyr idles the CPU between radio events. Display supply
 *              cut after CONFIG_ONEPAGE_EPD_POWER_HOLD_MS, NOR flash in
 *              deep power-down when idle (PM device runtime), touch chip
 *              in its own low-power mode after 2 s.
 *   Disconnected  Same, with slow advertising (ble_core.c).
 *   Active     Screen updating, touch in use, flash on while reading.
 *   Charging   As active, with the Charging screen.
 */
#include "power.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/poweroff.h>

#include "drivers/epd.h"
#include "drivers/input.h"
#include "drivers/power_hw.h"
#include "storage/books.h"
#include "ui/gfx.h"
#include "ui/screens.h"

LOG_MODULE_REGISTER(power, LOG_LEVEL_INF);

void power_off(const char *title, const char *body)
{
	LOG_INF("switching off: %s", title);
	alerts_stop();
	books_close();
	screens_draw_message(title, body);
	(void)epd_show(gfx_fb, EPD_FULL, 0, EPD_HEIGHT - 1);
	epd_power_off();
	(void)bt_disable();
	button_prepare_poweroff();
	charger_prepare_poweroff();
	k_msleep(50); /* let the log drain */
	sys_poweroff();
}

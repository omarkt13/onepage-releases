/*
 * OnePage Mini - MAX17048 fuel gauge through Zephyr's fuel gauge driver.
 * TODO(datasheet): load the custom model for the 4.35 V cell (RCOMP and
 * the model table from Maxim's characterisation) once the cell is chosen.
 */
#include <zephyr/device.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app.h"
#include "drivers/power_hw.h"

LOG_MODULE_REGISTER(battery, LOG_LEVEL_INF);

#if DT_HAS_COMPAT_STATUS_OKAY(maxim_max17048)
static const struct device *gauge = DEVICE_DT_GET_ONE(maxim_max17048);
#else
static const struct device *gauge;
#endif

int battery_init(void)
{
	if (!gauge || !device_is_ready(gauge)) {
		LOG_WRN("fuel gauge not found; battery level unknown");
		return -ENODEV;
	}
	return battery_update();
}

int battery_update(void)
{
	union fuel_gauge_prop_val soc, mv;

	if (!gauge || !device_is_ready(gauge)) {
		return -ENODEV;
	}
	int err = fuel_gauge_get_prop(gauge, FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE, &soc);

	if (!err) {
		err = fuel_gauge_get_prop(gauge, FUEL_GAUGE_VOLTAGE, &mv);
	}
	if (err) {
		return err;
	}
	app_lock();
	g_app.battery.gauge_ok = true;
	g_app.battery.percent = MIN(soc.relative_state_of_charge, 100);
	g_app.battery.millivolts = (uint16_t)(mv.voltage / 1000);
	g_app.battery.charging = charger_state();
	app_unlock();
	return 0;
}

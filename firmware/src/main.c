/*
 * OnePage Mini firmware - start-up. See README.md for the module map.
 */
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app.h"
#include "ble/ble.h"
#include "drivers/epd.h"
#include "drivers/input.h"
#include "drivers/power_hw.h"
#include "storage/books.h"
#include "storage/store.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

void app_init_state(void);
void app_run(void);

int main(void)
{
	int err;

	LOG_INF("OnePage Mini %s", CONFIG_ONEPAGE_FW_VERSION);
	app_init_state();

	/* Parts that may be missing on a dev kit log it and are skipped. */
	(void)alerts_hw_init();
	(void)epd_init();
	(void)charger_init();
	(void)battery_init();
	(void)button_init();
	(void)touch_init();
	(void)books_init();

	err = ble_init();
	if (err) {
		LOG_ERR("Bluetooth failed to start (%d)", err);
	}
	/* After bt_enable: settings also restore the bond. */
	err = store_init();
	if (err) {
		LOG_ERR("settings failed to load (%d)", err);
		app_set_error(OP_ERR_STORAGE_FAULT);
	}
	ble_start();
	(void)nfc_tag_init(ble_device_id());

	/* The image started and the radio runs: keep it (MCUboot would roll back). */
	if (!boot_is_img_confirmed()) {
		err = boot_write_img_confirmed();
		LOG_INF("image confirmed (%d)", err);
	}

	app_run();
	return 0;
}

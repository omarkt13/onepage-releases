/*
 * OnePage Mini firmware - start-up. See README.md for the module map.
 */
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
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

	int ble_err = ble_init();

	if (ble_err) {
		LOG_ERR("Bluetooth failed to start (%d)", ble_err);
	}
	/* After bt_enable: settings also restore the bond. */
	err = store_init();
	if (err) {
		LOG_ERR("settings failed to load (%d)", err);
		app_set_error(OP_ERR_STORAGE_FAULT);
	}
	ble_start();
	(void)nfc_tag_init(ble_device_id());

	/*
	 * Keep a new image only if Bluetooth started: an image without a radio
	 * cannot be updated again, so leave it unconfirmed and MCUboot returns
	 * to the previous image at the next restart (the watchdog forces one).
	 */
	if (!boot_is_img_confirmed()) {
		if (ble_err == 0) {
			err = boot_write_img_confirmed();
			LOG_INF("image confirmed (%d)", err);
		} else {
			LOG_ERR("new image not confirmed: Bluetooth failed; restarting to roll back");
			k_sleep(K_SECONDS(2));
			sys_reboot(SYS_REBOOT_COLD);
		}
	}

	app_run();
	return 0;
}

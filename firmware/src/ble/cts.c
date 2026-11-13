/*
 * OnePage Mini - Current Time Service client. The iPhone gives local wall
 * clock time, which is what the device shows and stores (protocol 1).
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <bluetooth/gatt_dm.h>
#include <bluetooth/services/cts_client.h>

#include "app.h"
#include "ble/ble.h"
#include "clock.h"
#include "core/op_time.h"

LOG_MODULE_REGISTER(cts, LOG_LEVEL_INF);

static struct bt_cts_client cts_c;

static void apply(const struct bt_cts_current_time *t)
{
	const struct bt_cts_exact_time_256 *e = &t->exact_time_256;

	if (e->year < 2000 || e->month < 1 || e->month > 12 || e->day < 1) {
		LOG_WRN("time not valid");
		return;
	}
	clock_set_local(op_time_from_civil(e->year, e->month, e->day, e->hours, e->minutes,
					   e->seconds));
	LOG_INF("clock %04u-%02u-%02u %02u:%02u", e->year, e->month, e->day, e->hours,
		e->minutes);
	app_post(EVT_TIME_SET, 0, 0);
}

static void read_cb(struct bt_cts_client *c, struct bt_cts_current_time *t, int err)
{
	ARG_UNUSED(c);
	if (!err) {
		apply(t);
	}
}

static void notify_cb(struct bt_cts_client *c, struct bt_cts_current_time *t)
{
	ARG_UNUSED(c);
	apply(t);
}

int cts_setup(void)
{
	return bt_cts_client_init(&cts_c);
}

int cts_assign(struct bt_gatt_dm *dm)
{
	return bt_cts_handles_assign(dm, &cts_c);
}

void cts_start(void)
{
	int err = bt_cts_read_current_time(&cts_c, read_cb);

	if (err) {
		LOG_WRN("time read failed (%d)", err);
	}
	/* Time zone and daylight saving changes arrive as notifications. */
	err = bt_cts_subscribe_current_time(&cts_c, notify_cb);
	if (err) {
		LOG_DBG("time notifications not available (%d)", err);
	}
}

void cts_reset(void)
{
}

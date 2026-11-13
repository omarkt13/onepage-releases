/*
 * OnePage Mini - Azoteq IQS7222A touch controller as a one-axis slider.
 *
 * The chip runs in event mode: it pulls RDY low when the slider changes.
 * While a finger is down, the driver also polls every 25 ms so a long
 * press is reported while the finger is still down.
 *
 * TODO(datasheet): register addresses and the configuration block must be
 * checked against the IQS7222A datasheet and the Azoteq GUI export for the
 * final electrode layout (50 x 6 mm slider with ground ring). The values
 * here follow the IQS7222A memory map as published (version 1.x) and set
 * the chip to drop to its lowest power mode 2 s after the last touch.
 */
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "app.h"
#include "core/gesture.h"
#include "drivers/input.h"

LOG_MODULE_REGISTER(touch, LOG_LEVEL_INF);

#define TOUCH_NODE DT_CHOSEN(onepage_touch)

/* ---- register map: TODO(datasheet) ------------------------------------------------ */
#define REG_PRODUCT_NUM 0x00
#define PRODUCT_IQS7222A 840
#define REG_SYS_STATUS 0x10
#define REG_EVENTS 0x11
#define REG_SLIDER0_OUT 0x14
#define REG_SYS_CONTROL 0xD0 /* system settings: control word */
#define REG_PWR_TIMEOUTS 0xD3 /* normal-to-low and low-to-ULP timeouts */
#define REG_SLIDER0_SETUP 0xB0
#define SLIDER_NO_TOUCH 0xFFFF
#define SLIDER_RESOLUTION 1000 /* configured output range 0..1000 */

#define SYS_CTRL_ACK_RESET BIT(0)
#define SYS_CTRL_EVENT_MODE BIT(6)
#define SYS_STATUS_RESET BIT(3)

struct reg_write {
	uint8_t reg;
	uint16_t value;
};

/* Settings that differ from the chip defaults. TODO(datasheet): replace with
 * the Azoteq GUI export once the electrode design is tuned.
 */
static const struct reg_write config[] = {
	{REG_SLIDER0_SETUP + 1, SLIDER_RESOLUTION}, /* slider output range */
	{REG_PWR_TIMEOUTS, 0x0214},                 /* 2 s to low power, then ULP */
};

#if DT_NODE_EXISTS(TOUCH_NODE)
static const struct i2c_dt_spec i2c = I2C_DT_SPEC_GET(TOUCH_NODE);
static const struct gpio_dt_spec rdy = GPIO_DT_SPEC_GET(TOUCH_NODE, rdy_gpios);
#define HAVE_TOUCH 1
#else
#define HAVE_TOUCH 0
#endif

static bool present;
static struct gesture_rec rec;
static struct k_work_delayable poll_work;
static struct gpio_callback rdy_cb;
static bool finger_down;

#if HAVE_TOUCH

static int rd16(uint8_t reg, uint16_t *val)
{
	uint8_t b[2];
	int err = i2c_write_read_dt(&i2c, &reg, 1, b, 2);

	if (!err) {
		*val = sys_get_le16(b);
	}
	return err;
}

static int wr16(uint8_t reg, uint16_t val)
{
	uint8_t b[3] = {reg, (uint8_t)val, (uint8_t)(val >> 8)};

	return i2c_write_dt(&i2c, b, sizeof(b));
}

static void deliver(enum gesture g)
{
	switch (g) {
	case GESTURE_TAP: app_post(EVT_INPUT, UI_IN_TAP, 0); break;
	case GESTURE_LONG_PRESS: app_post(EVT_INPUT, UI_IN_LONG, 0); break;
	case GESTURE_SWIPE_FWD: app_post(EVT_INPUT, UI_IN_FWD, 0); break;
	case GESTURE_SWIPE_BACK: app_post(EVT_INPUT, UI_IN_BACK, 0); break;
	default: break;
	}
}

static void poll_fn(struct k_work *w)
{
	uint16_t pos = SLIDER_NO_TOUCH;
	uint16_t status;

	ARG_UNUSED(w);
	if (rd16(REG_SYS_STATUS, &status) == 0 && (status & SYS_STATUS_RESET)) {
		LOG_WRN("touch controller reset: configuring again");
		touch_init();
		return;
	}
	if (rd16(REG_SLIDER0_OUT, &pos)) {
		pos = SLIDER_NO_TOUCH;
	}
	bool touching = pos != SLIDER_NO_TOUCH;
	int16_t p = touching ? (int16_t)((uint32_t)pos * GESTURE_POS_MAX / SLIDER_RESOLUTION) : 0;

	deliver(gesture_feed(&rec, k_uptime_get_32(), touching, p));
	finger_down = touching;
	if (touching) {
		k_work_reschedule(&poll_work, K_MSEC(25));
	}
}

static void rdy_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	k_work_reschedule(&poll_work, K_NO_WAIT);
}

int touch_init(void)
{
	uint16_t product = 0;
	int err;

	k_work_init_delayable(&poll_work, poll_fn);
	gesture_init(&rec);
	if (!i2c_is_ready_dt(&i2c) || !gpio_is_ready_dt(&rdy)) {
		LOG_WRN("touch bus not ready");
		return -ENODEV;
	}
	err = rd16(REG_PRODUCT_NUM, &product);
	if (err) {
		LOG_WRN("touch controller not found (%d); running without it", err);
		return -ENODEV;
	}
	if (product != PRODUCT_IQS7222A) {
		LOG_WRN("unexpected touch product number %u", product);
	}
	for (size_t i = 0; i < ARRAY_SIZE(config); i++) {
		err = err ?: wr16(config[i].reg, config[i].value);
	}
	err = err ?: wr16(REG_SYS_CONTROL, SYS_CTRL_ACK_RESET | SYS_CTRL_EVENT_MODE);
	if (err) {
		LOG_ERR("touch configuration failed (%d)", err);
		app_set_error(OP_ERR_TOUCH);
		return err;
	}
	if (!present) {
		gpio_pin_configure_dt(&rdy, GPIO_INPUT);
		gpio_init_callback(&rdy_cb, rdy_isr, BIT(rdy.pin));
		gpio_add_callback_dt(&rdy, &rdy_cb);
		gpio_pin_interrupt_configure_dt(&rdy, GPIO_INT_EDGE_TO_ACTIVE);
	}
	present = true;
	LOG_INF("touch ready");
	return 0;
}

#else

int touch_init(void)
{
	LOG_WRN("no touch controller in devicetree");
	return -ENODEV;
}

#endif

bool touch_present(void)
{
	return present;
}

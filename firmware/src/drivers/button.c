/*
 * OnePage Mini - power button. Short press: wake or Home. Hold 3 s: power
 * menu. The 10 s forced restart is done in hardware and needs no code.
 */
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app.h"
#include "drivers/input.h"

LOG_MODULE_REGISTER(button, LOG_LEVEL_INF);

#define HOLD_MS 3000
#define DEBOUNCE_MS 20

#if DT_NODE_EXISTS(DT_ALIAS(sw0))
static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
#define HAVE_BUTTON 1
#else
#define HAVE_BUTTON 0
#endif

static struct gpio_callback cb;
static struct k_work_delayable debounce_work;
static struct k_work_delayable hold_work;
static bool pressed;
static bool hold_sent;

#if HAVE_BUTTON
static void hold_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	if (pressed) {
		hold_sent = true;
		app_post(EVT_INPUT, UI_IN_BTN_HOLD, 0);
	}
}

static void debounce_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	bool now = gpio_pin_get_dt(&btn) == 1;

	if (now == pressed) {
		return;
	}
	pressed = now;
	if (pressed) {
		hold_sent = false;
		k_work_reschedule(&hold_work, K_MSEC(HOLD_MS));
	} else {
		k_work_cancel_delayable(&hold_work);
		if (!hold_sent) {
			app_post(EVT_INPUT, UI_IN_BTN_SHORT, 0);
		}
	}
}

static void isr(const struct device *dev, struct gpio_callback *c, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(c);
	ARG_UNUSED(pins);
	k_work_reschedule(&debounce_work, K_MSEC(DEBOUNCE_MS));
}

int button_init(void)
{
	k_work_init_delayable(&debounce_work, debounce_fn);
	k_work_init_delayable(&hold_work, hold_fn);
	if (!gpio_is_ready_dt(&btn)) {
		return -ENODEV;
	}
	gpio_pin_configure_dt(&btn, GPIO_INPUT);
	gpio_init_callback(&cb, isr, BIT(btn.pin));
	gpio_add_callback_dt(&btn, &cb);
	return gpio_pin_interrupt_configure_dt(&btn, GPIO_INT_EDGE_BOTH);
}

void button_prepare_poweroff(void)
{
	/* A level interrupt on the pin arms the GPIO sense for System OFF wake. */
	gpio_pin_interrupt_configure_dt(&btn, GPIO_INT_LEVEL_ACTIVE);
}

#else

int button_init(void)
{
	LOG_WRN("no power button in devicetree");
	return -ENODEV;
}

void button_prepare_poweroff(void)
{
}

#endif

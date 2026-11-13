/*
 * OnePage Mini - MCP73833 charger status. STAT1/STAT2 are open drain,
 * active low:  L/H charging, H/L complete, H/H not charging, L/L fault.
 */
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app.h"
#include "drivers/power_hw.h"

LOG_MODULE_REGISTER(charger, LOG_LEVEL_INF);

#define CHG_NODE DT_CHOSEN(onepage_charger)

#if DT_NODE_EXISTS(CHG_NODE)
static const struct gpio_dt_spec stat1 = GPIO_DT_SPEC_GET(CHG_NODE, stat1_gpios);
static const struct gpio_dt_spec stat2 = GPIO_DT_SPEC_GET(CHG_NODE, stat2_gpios);
#define HAVE_CHARGER 1
#else
#define HAVE_CHARGER 0
#endif

static uint8_t state = OP_CHG_NONE;
static struct gpio_callback cb1, cb2;
static struct k_work_delayable settle_work;

#if HAVE_CHARGER
static uint8_t read_state(void)
{
	bool s1 = gpio_pin_get_dt(&stat1) == 1; /* 1 = pin low */
	bool s2 = gpio_pin_get_dt(&stat2) == 1;

	if (s1 && !s2) {
		return OP_CHG_CHARGING;
	}
	if (!s1 && s2) {
		return OP_CHG_FULL;
	}
	if (s1 && s2) {
		return OP_CHG_FAULT;
	}
	return OP_CHG_NONE;
}

static void settle_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	uint8_t s = read_state();

	if (s != state) {
		state = s;
		LOG_INF("charger state %u", s);
		app_post(EVT_CHARGER, s, 0);
	}
}

static void isr(const struct device *dev, struct gpio_callback *c, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(c);
	ARG_UNUSED(pins);
	k_work_reschedule(&settle_work, K_MSEC(50));
}

int charger_init(void)
{
	k_work_init_delayable(&settle_work, settle_fn);
	if (!gpio_is_ready_dt(&stat1) || !gpio_is_ready_dt(&stat2)) {
		return -ENODEV;
	}
	gpio_pin_configure_dt(&stat1, GPIO_INPUT);
	gpio_pin_configure_dt(&stat2, GPIO_INPUT);
	gpio_init_callback(&cb1, isr, BIT(stat1.pin));
	gpio_init_callback(&cb2, isr, BIT(stat2.pin));
	gpio_add_callback_dt(&stat1, &cb1);
	gpio_add_callback_dt(&stat2, &cb2);
	gpio_pin_interrupt_configure_dt(&stat1, GPIO_INT_EDGE_BOTH);
	gpio_pin_interrupt_configure_dt(&stat2, GPIO_INT_EDGE_BOTH);
	state = read_state();
	return 0;
}

void charger_prepare_poweroff(void)
{
	/* Plugging in the charger pulls STAT1 low: that wakes the device. */
	gpio_pin_interrupt_configure_dt(&stat1, GPIO_INT_LEVEL_ACTIVE);
}
#else
int charger_init(void)
{
	LOG_WRN("no charger status pins in devicetree");
	return -ENODEV;
}

void charger_prepare_poweroff(void)
{
}
#endif

uint8_t charger_state(void)
{
	return state;
}

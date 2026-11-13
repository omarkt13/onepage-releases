/*
 * OnePage Mini - LED and vibration patterns. The DRV2605L drives the LRA
 * from its built-in LRA effect library, so the processor only starts it.
 * The LED pattern is a short timed sequence (under 1 s).
 */
#include <zephyr/device.h>
#include <zephyr/drivers/haptics.h>
#include <zephyr/drivers/haptics/drv2605.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app.h"
#include "drivers/power_hw.h"

LOG_MODULE_REGISTER(alerts_hw, LOG_LEVEL_INF);

#if DT_NODE_EXISTS(DT_ALIAS(pwm_led0))
static const struct pwm_dt_spec led = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0));
#define HAVE_LED 1
#else
#define HAVE_LED 0
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(ti_drv2605)
static const struct device *haptic = DEVICE_DT_GET_ONE(ti_drv2605);
#else
static const struct device *haptic;
#endif

/* DRV2605 LRA library effect IDs, by strength 1..3. */
static const uint8_t fx_click[4] = {0, 3, 2, 1};   /* strong click 30/60/100% */
static const uint8_t fx_double[4] = {0, 11, 11, 10}; /* double click 60/60/100% */
static const uint8_t fx_buzz[4] = {0, 49, 48, 47};   /* buzz 60/80/100% */

/* LED steps in ms: on, off, on, ... (0 ends). */
static const uint16_t led_single[] = {120, 0};
static const uint16_t led_double[] = {120, 150, 120, 0};
static const uint16_t led_long[] = {700, 0};

static const uint16_t *led_seq;
static uint8_t led_step;
static struct k_work_delayable led_work;
static bool haptic_ok;

static void led_set(bool on)
{
#if HAVE_LED
	(void)pwm_set_pulse_dt(&led, on ? led.period : 0);
#else
	ARG_UNUSED(on);
#endif
}

static void led_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	if (!led_seq || led_seq[led_step] == 0) {
		led_set(false);
		led_seq = NULL;
		return;
	}
	bool on = (led_step % 2) == 0;

	led_set(on);
	k_work_reschedule(&led_work, K_MSEC(led_seq[led_step]));
	led_step++;
}

static void haptic_play(uint8_t effect)
{
	if (!haptic_ok || effect == 0) {
		return;
	}
	struct drv2605_rom_data rom = {
		.trigger = DRV2605_MODE_INTERNAL_TRIGGER,
		.library = DRV2605_LIBRARY_LRA,
		.seq_regs = {effect, 0},
	};
	union drv2605_config_data cfg = {.rom_data = &rom};

	if (drv2605_haptic_config(haptic, DRV2605_HAPTICS_SOURCE_ROM, &cfg) == 0) {
		(void)haptics_start_output(haptic);
	}
}

int alerts_hw_init(void)
{
	k_work_init_delayable(&led_work, led_fn);
#if HAVE_LED
	if (!pwm_is_ready_dt(&led)) {
		LOG_WRN("LED not ready");
	}
#endif
	haptic_ok = haptic && device_is_ready(haptic);
	if (!haptic_ok) {
		LOG_WRN("haptic driver not found; vibration off");
	}
	led_set(false);
	return 0;
}

void alerts_play(uint8_t pattern, uint8_t strength)
{
	if (strength == 0 || strength > 3) {
		return;
	}
	switch (pattern) {
	case OP_PATTERN_SINGLE:
		led_seq = led_single;
		haptic_play(fx_click[strength]);
		break;
	case OP_PATTERN_DOUBLE:
		led_seq = led_double;
		haptic_play(fx_double[strength]);
		break;
	case OP_PATTERN_LONG:
		led_seq = led_long;
		haptic_play(fx_buzz[strength]);
		break;
	default:
		return;
	}
	led_step = 0;
	k_work_reschedule(&led_work, K_NO_WAIT);
}

void alerts_stop(void)
{
	led_seq = NULL;
	k_work_cancel_delayable(&led_work);
	led_set(false);
	if (haptic_ok) {
		(void)haptics_stop_output(haptic);
	}
}

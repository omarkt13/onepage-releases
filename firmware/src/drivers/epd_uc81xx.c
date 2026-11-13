/*
 * OnePage Mini - driver for the Sunshine SDE0368NQW-52 (528 x 792) on an
 * UltraChip UC81xx-family controller.
 *
 * TODO(datasheet): the panel's controller part and its command values are
 * not confirmed. Every controller-specific byte is in the table below
 * (modelled on the UC8179 command set) so a datasheet check changes one
 * place. LUTs for the fast and partial modes are taken from OTP through the
 * temperature-override trick commonly used with UC8179 panels; replace with
 * the panel vendor's LUTs once supplied.
 */
#include "drivers/epd.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app.h"

LOG_MODULE_REGISTER(epd, LOG_LEVEL_INF);

#define EPD_NODE DT_CHOSEN(onepage_display)

/* ---- controller command table: TODO(datasheet) ---------------------------------- */

struct epd_cmd {
	uint8_t cmd;
	uint8_t len;
	uint8_t data[6];
};

#define CMD_PSR 0x00   /* panel setting */
#define CMD_PWR 0x01   /* power setting */
#define CMD_POF 0x02   /* power off */
#define CMD_PON 0x04   /* power on */
#define CMD_BTST 0x06  /* booster soft start */
#define CMD_DSLP 0x07  /* deep sleep, data 0xA5 */
#define CMD_DTM1 0x10  /* old data */
#define CMD_DRF 0x12   /* display refresh */
#define CMD_DTM2 0x13  /* new data */
#define CMD_CDI 0x50   /* VCOM and data interval */
#define CMD_TCON 0x60
#define CMD_TRES 0x61  /* resolution */
#define CMD_PTL 0x90   /* partial window */
#define CMD_PTIN 0x91  /* partial in */
#define CMD_PTOUT 0x92 /* partial out */
#define CMD_CCSET 0xE0 /* cascade setting: enables temperature override */
#define CMD_TSSET 0xE5 /* force temperature value (selects OTP LUT) */

static const struct epd_cmd init_seq[] = {
	{CMD_PWR, 4, {0x07, 0x07, 0x3F, 0x3F}},
	{CMD_BTST, 4, {0x17, 0x17, 0x28, 0x17}},
	{CMD_PSR, 1, {0x1F}}, /* KW mode, LUT from OTP, scan up, shift right */
	{CMD_TRES, 4, {EPD_WIDTH >> 8, EPD_WIDTH & 0xFF, EPD_HEIGHT >> 8, EPD_HEIGHT & 0xFF}},
	{CMD_CDI, 2, {0x29, 0x07}}, /* N2OCP: new data copied to old after refresh */
	{CMD_TCON, 1, {0x22}},
};

/* Temperature override values that select faster OTP waveforms. */
static const struct epd_cmd mode_full[] = {{CMD_CCSET, 1, {0x00}}};
static const struct epd_cmd mode_fast[] = {{CMD_CCSET, 1, {0x02}}, {CMD_TSSET, 1, {0x5A}}};
static const struct epd_cmd mode_partial[] = {{CMD_CCSET, 1, {0x02}}, {CMD_TSSET, 1, {0x6E}}};

/* Controller RAM polarity in KW mode: 1 = white. Our frame uses 1 = black. */
#define EPD_INVERT 1
#define EPD_BUSY_TIMEOUT_MS 8000
#define EPD_FULL_EVERY 10

/* ---------------------------------------------------------------------------------- */

#if DT_NODE_EXISTS(EPD_NODE)
static const struct spi_dt_spec spi =
	SPI_DT_SPEC_GET(EPD_NODE, SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0);
static const struct gpio_dt_spec dc = GPIO_DT_SPEC_GET(EPD_NODE, dc_gpios);
static const struct gpio_dt_spec rst = GPIO_DT_SPEC_GET(EPD_NODE, reset_gpios);
static const struct gpio_dt_spec busy = GPIO_DT_SPEC_GET(EPD_NODE, busy_gpios);
static const struct gpio_dt_spec pwr = GPIO_DT_SPEC_GET_OR(EPD_NODE, power_gpios, {0});
#define HAVE_EPD 1
#else
#define HAVE_EPD 0
#endif

static bool present;
static bool powered;       /* supply on, controller initialised, RAM valid */
static bool ram_valid;     /* controller holds the last image as "old" data */
static uint8_t partials;
static struct k_work_delayable power_off_work;
static K_MUTEX_DEFINE(epd_lock);

#if HAVE_EPD

static int write_cmd(uint8_t cmd, const uint8_t *data, size_t len)
{
	struct spi_buf b = {.buf = &cmd, .len = 1};
	struct spi_buf_set s = {.buffers = &b, .count = 1};
	int err;

	gpio_pin_set_dt(&dc, 0);
	err = spi_write_dt(&spi, &s);
	if (err || len == 0) {
		return err;
	}
	gpio_pin_set_dt(&dc, 1);
	b.buf = (void *)data;
	b.len = len;
	return spi_write_dt(&spi, &s);
}

static int run(const struct epd_cmd *seq, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		int err = write_cmd(seq[i].cmd, seq[i].data, seq[i].len);

		if (err) {
			return err;
		}
	}
	return 0;
}

static int busy_timeout_ms = 2000; /* short at probe time */

static int wait_idle(void)
{
	int64_t end = k_uptime_get() + busy_timeout_ms;

	/* busy-gpios is flagged active-low: get_dt returns 1 while busy. */
	while (gpio_pin_get_dt(&busy) == 1) {
		if (k_uptime_get() > end) {
			return -ETIMEDOUT;
		}
		k_msleep(5);
	}
	return 0;
}

static int power_up(void)
{
	int err;

	if (powered) {
		return 0;
	}
	if (pwr.port) {
		gpio_pin_set_dt(&pwr, 1);
		k_msleep(5);
	}
	gpio_pin_set_dt(&rst, 1);
	k_msleep(10);
	gpio_pin_set_dt(&rst, 0);
	k_msleep(10);
	err = wait_idle();
	if (!err) {
		err = run(init_seq, ARRAY_SIZE(init_seq));
	}
	if (err) {
		return err;
	}
	powered = true;
	ram_valid = false;
	return 0;
}

static void send_rows(uint8_t cmd, const uint8_t *fb, uint16_t y0, uint16_t y1)
{
	static uint8_t row[EPD_STRIDE];

	write_cmd(cmd, NULL, 0);
	gpio_pin_set_dt(&dc, 1);
	for (uint16_t y = y0; y <= y1; y++) {
		const uint8_t *src = fb + (size_t)y * EPD_STRIDE;

		for (int i = 0; i < EPD_STRIDE; i++) {
			row[i] = EPD_INVERT ? (uint8_t)~src[i] : src[i];
		}
		struct spi_buf b = {.buf = row, .len = EPD_STRIDE};
		struct spi_buf_set s = {.buffers = &b, .count = 1};

		(void)spi_write_dt(&spi, &s);
	}
}

static int refresh(void)
{
	int err = write_cmd(CMD_PON, NULL, 0);

	err = err ?: wait_idle();
	err = err ?: write_cmd(CMD_DRF, NULL, 0);
	k_msleep(1);
	err = err ?: wait_idle();
	/* Charge pump off; RAM is kept while the supply stays on. */
	(void)write_cmd(CMD_POF, NULL, 0);
	(void)wait_idle();
	return err;
}

static void power_off_locked(void)
{
	if (!powered) {
		return;
	}
	static const uint8_t dslp = 0xA5;

	(void)write_cmd(CMD_DSLP, &dslp, 1);
	if (pwr.port) {
		gpio_pin_set_dt(&pwr, 0);
	}
	powered = false;
	ram_valid = false;
}

static void power_off_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	k_mutex_lock(&epd_lock, K_FOREVER);
	power_off_locked();
	k_mutex_unlock(&epd_lock);
}

int epd_show(const uint8_t *fb, enum epd_mode mode, uint16_t y0, uint16_t y1)
{
	int err;

	if (!present) {
		return -ENODEV;
	}
	k_mutex_lock(&epd_lock, K_FOREVER);
	k_work_cancel_delayable(&power_off_work);
	err = power_up();
	if (err) {
		goto out;
	}
	if (!ram_valid || (mode != EPD_FULL && partials >= EPD_FULL_EVERY)) {
		mode = EPD_FULL;
	}
	if (y1 >= EPD_HEIGHT) {
		y1 = EPD_HEIGHT - 1;
	}
	switch (mode) {
	case EPD_PARTIAL: {
		uint8_t win[9] = {0, 0, (EPD_WIDTH - 1) >> 8, (EPD_WIDTH - 1) & 0xFF,
				  y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF, 0x01};

		err = run(mode_partial, ARRAY_SIZE(mode_partial));
		err = err ?: write_cmd(CMD_PTIN, NULL, 0);
		err = err ?: write_cmd(CMD_PTL, win, sizeof(win));
		if (!err) {
			send_rows(CMD_DTM2, fb, y0, y1);
			err = refresh();
		}
		(void)write_cmd(CMD_PTOUT, NULL, 0);
		partials++;
		break;
	}
	case EPD_FAST:
		err = run(mode_fast, ARRAY_SIZE(mode_fast));
		if (!err) {
			send_rows(CMD_DTM2, fb, 0, EPD_HEIGHT - 1);
			err = refresh();
		}
		partials++;
		break;
	default:
		err = run(mode_full, ARRAY_SIZE(mode_full));
		if (!err) {
			send_rows(CMD_DTM2, fb, 0, EPD_HEIGHT - 1);
			err = refresh();
		}
		partials = 0;
		break;
	}
	ram_valid = err == 0;
out:
	if (err) {
		LOG_ERR("refresh failed (%d)", err);
		app_set_error(OP_ERR_DISPLAY);
		power_off_locked();
	} else {
		k_work_reschedule(&power_off_work, K_MSEC(CONFIG_ONEPAGE_EPD_POWER_HOLD_MS));
	}
	k_mutex_unlock(&epd_lock);
	return err;
}

void epd_power_off(void)
{
	k_work_cancel_delayable(&power_off_work);
	power_off_fn(NULL);
}

int epd_init(void)
{
	k_work_init_delayable(&power_off_work, power_off_fn);
	if (!spi_is_ready_dt(&spi) || !gpio_is_ready_dt(&dc) || !gpio_is_ready_dt(&rst) ||
	    !gpio_is_ready_dt(&busy)) {
		LOG_WRN("display bus not ready");
		return -ENODEV;
	}
	gpio_pin_configure_dt(&dc, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&rst, GPIO_OUTPUT_INACTIVE);
	/* Pulled to the busy level: with no panel fitted, probing times out. */
	gpio_pin_configure_dt(&busy, GPIO_INPUT | GPIO_PULL_DOWN);
	if (pwr.port) {
		gpio_pin_configure_dt(&pwr, GPIO_OUTPUT_INACTIVE);
	}
	k_mutex_lock(&epd_lock, K_FOREVER);
	int err = power_up();

	k_mutex_unlock(&epd_lock);
	if (err) {
		/* BUSY never released: no panel on this board (for example a bare DK). */
		LOG_WRN("display not responding (%d); running without it", err);
		power_off_fn(NULL);
		return -ENODEV;
	}
	present = true;
	busy_timeout_ms = EPD_BUSY_TIMEOUT_MS;
	LOG_INF("display ready");
	return 0;
}

#else /* !HAVE_EPD */

int epd_show(const uint8_t *fb, enum epd_mode mode, uint16_t y0, uint16_t y1)
{
	return -ENODEV;
}

void epd_power_off(void)
{
}

int epd_init(void)
{
	LOG_WRN("no display in devicetree");
	return -ENODEV;
}

#endif

bool epd_present(void)
{
	return present;
}

uint8_t epd_partial_count(void)
{
	return partials;
}

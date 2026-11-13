/*
 * OnePage Mini - NFC tag (protocol section 4). The built-in NFC-A Type 2
 * tag holds https://onepage.app/p/<DEVICE_ID>. A phone's field wakes the
 * device; the application decides whether to open the pairing window.
 */
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <nfc/ndef/uri_msg.h>
#include <nfc_t2t_lib.h>

#include "app.h"
#include "ble/ble.h"

LOG_MODULE_REGISTER(nfc_tag, LOG_LEVEL_INF);

static uint8_t ndef_buf[96];

static void nfc_cb(void *ctx, nfc_t2t_event_t event, const uint8_t *data, size_t len)
{
	ARG_UNUSED(ctx);
	ARG_UNUSED(data);
	ARG_UNUSED(len);
	if (event == NFC_T2T_EVENT_FIELD_ON) {
		app_post(EVT_NFC_FIELD, 0, 0);
	}
}

int nfc_tag_init(const char *device_id)
{
	char uri[64];
	uint32_t len = sizeof(ndef_buf);
	int n = snprintf(uri, sizeof(uri), "%s%s", CONFIG_ONEPAGE_NFC_URL_HOST, device_id);
	int err;

	if (n <= 0 || n >= (int)sizeof(uri)) {
		return -EINVAL;
	}
	err = nfc_t2t_setup(nfc_cb, NULL);
	if (err) {
		LOG_ERR("NFC setup failed (%d)", err);
		return err;
	}
	err = nfc_ndef_uri_msg_encode(NFC_URI_HTTPS, (const uint8_t *)uri, (uint16_t)n, ndef_buf,
				      &len);
	if (!err) {
		err = nfc_t2t_payload_set(ndef_buf, len);
	}
	if (!err) {
		err = nfc_t2t_emulation_start();
	}
	if (err) {
		LOG_ERR("NFC tag failed (%d)", err);
	} else {
		LOG_INF("NFC tag: https://%s", uri);
	}
	return err;
}

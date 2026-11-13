/*
 * OnePage Mini - codecs for every characteristic in docs/protocol.md.
 * Plain C99. The GATT layer passes raw attribute values in and out.
 */
#ifndef OP_PROTO_H
#define OP_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "op_util.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OP_PROTO_VERSION 1

/* Short IDs inside the base UUID 4f50xxxx-6d69-6e69-8000-6f6e65706167. */
enum op_uuid_short {
	OP_UUID_SERVICE = 0x0001,
	OP_UUID_STATUS = 0x0002,
	OP_UUID_SETTINGS = 0x0003,
	OP_UUID_APP_FILTER = 0x0004,
	OP_UUID_HOME_DATA = 0x0005,
	OP_UUID_CALENDAR = 0x0006,
	OP_UUID_DATA_REQUEST = 0x0007,
	OP_UUID_PRIVACY = 0x0008,
	OP_UUID_XFER_CTRL = 0x0009,
	OP_UUID_XFER_DATA = 0x000A,
};

/* Error codes (protocol section 7). */
enum op_error {
	OP_ERR_NONE = 0x0000,
	OP_ERR_VERSION = 0x0001,
	OP_ERR_MALFORMED = 0x0002,
	OP_ERR_STORAGE_FULL = 0x0003,
	OP_ERR_STORAGE_FAULT = 0x0004,
	OP_ERR_CALENDAR = 0x0005,
	OP_ERR_BOOK = 0x0006,
	OP_ERR_DISPLAY = 0x0007,
	OP_ERR_TOUCH = 0x0008,
};

/* Result of parsing an incoming message. */
enum op_parse {
	OP_PARSE_OK = 0,
	OP_PARSE_BAD_VERSION = -1,
	OP_PARSE_MALFORMED = -2,
};

/* ---- Status 0x0002 ---------------------------------------------------- */

enum op_charging { OP_CHG_NONE = 0, OP_CHG_CHARGING = 1, OP_CHG_FULL = 2, OP_CHG_FAULT = 3 };

struct op_status {
	uint8_t battery;
	uint8_t charging;
	char firmware[16];
	uint32_t storage_free_kib;
	uint32_t storage_total_kib;
	uint16_t last_error;
	uint32_t calendar_version;
	uint32_t calendar_synced_at;
	uint8_t hw_rev;
};

size_t op_status_encode(const struct op_status *s, uint8_t *buf, size_t cap);
int op_status_decode(struct op_status *s, const uint8_t *buf, size_t len);

/* ---- Settings 0x0003 -------------------------------------------------- */

enum op_refresh_mode { OP_REFRESH_QUALITY = 0, OP_REFRESH_BALANCED = 1, OP_REFRESH_FAST = 2 };

struct op_settings {
	uint8_t quiet_on;
	uint16_t quiet_start; /* minutes after midnight */
	uint16_t quiet_end;
	uint8_t refresh_mode;
	uint8_t clock_12h;
	char language[3];
	uint8_t alert_strength; /* 0..3 */
};

void op_settings_default(struct op_settings *s);
size_t op_settings_encode(const struct op_settings *s, uint8_t *buf, size_t cap);
/* Applies only the tags present and valid. Unknown tags are skipped. */
int op_settings_apply(struct op_settings *s, const uint8_t *buf, size_t len);

/* ---- App filter 0x0004 ------------------------------------------------ */

#define OP_APP_ID_MAX 64

enum op_filter_mode { OP_MODE_SHOW = 0, OP_MODE_SILENT = 1, OP_MODE_HIDE = 2 };
enum op_alert_pattern {
	OP_PATTERN_DEFAULT = 0,
	OP_PATTERN_SINGLE = 1,
	OP_PATTERN_DOUBLE = 2,
	OP_PATTERN_LONG = 3,
	OP_PATTERN_NONE = 4,
};
#define OP_FILTER_FLAG_TITLE_ONLY 0x01

struct op_filter_entry {
	uint8_t mode;
	uint8_t pattern;
	uint8_t flags;
	char id[OP_APP_ID_MAX + 1];
};

enum op_filter_op {
	OP_FILTER_LIST = 0x01,
	OP_FILTER_SET = 0x02,
	OP_FILTER_REMOVE = 0x03,
	OP_FILTER_LIST_REPLY = 0x81,
	OP_FILTER_SET_REPLY = 0x82,
	OP_FILTER_REMOVE_REPLY = 0x83,
};

struct op_filter_cmd {
	uint8_t op;
	uint16_t start;               /* LIST */
	struct op_filter_entry entry; /* SET; REMOVE uses entry.id only */
};

void op_filter_entry_write(struct op_wr *w, const struct op_filter_entry *e);
bool op_filter_entry_read(struct op_rd *r, struct op_filter_entry *e);
size_t op_filter_entry_size(const struct op_filter_entry *e);
int op_filter_cmd_parse(struct op_filter_cmd *c, const uint8_t *buf, size_t len);
size_t op_filter_simple_reply(uint8_t op, uint8_t result, uint8_t *buf, size_t cap);

/* ---- Home data 0x0005 ------------------------------------------------- */

enum op_weather_cond {
	OP_WX_UNKNOWN = 0, OP_WX_CLEAR, OP_WX_PARTLY_CLOUDY, OP_WX_CLOUDY, OP_WX_FOG,
	OP_WX_DRIZZLE, OP_WX_RAIN, OP_WX_HEAVY_RAIN, OP_WX_SNOW, OP_WX_SLEET,
	OP_WX_THUNDER, OP_WX_WIND, OP_WX_HOT, OP_WX_COLD, OP_WX_COUNT
};

struct op_weather {
	bool valid;
	int16_t temp_now; /* tenths of a degree C */
	int16_t temp_high;
	int16_t temp_low;
	uint8_t condition;
	uint32_t valid_until; /* local seconds */
	char place[25];
	uint8_t unit; /* 0 C, 1 F */
};

int op_weather_apply(struct op_weather *w, const uint8_t *buf, size_t len);
size_t op_weather_encode(const struct op_weather *w, uint8_t *buf, size_t cap);

/* ---- Calendar sync 0x0006 --------------------------------------------- */

enum op_cal_op {
	OP_CAL_BEGIN = 0x01,
	OP_CAL_DATA = 0x02,
	OP_CAL_COMMIT = 0x03,
	OP_CAL_CLEAR = 0x04,
	OP_CAL_ACK = 0x80,
};

enum op_cal_status {
	OP_CAL_OK = 0,
	OP_CAL_OUT_OF_ORDER = 1,
	OP_CAL_CRC_MISMATCH = 2,
	OP_CAL_TOO_LARGE = 3,
	OP_CAL_BAD_CONTENT = 4,
	OP_CAL_NO_BEGIN = 5,
};

struct op_cal_cmd {
	uint8_t op;
	uint32_t version;
	uint32_t total_len;
	uint32_t crc32;
	uint32_t offset;
	const uint8_t *data;
	size_t data_len;
};

int op_cal_cmd_parse(struct op_cal_cmd *c, const uint8_t *buf, size_t len);
size_t op_cal_ack_encode(uint8_t status, uint32_t version, uint32_t next_offset, uint8_t *buf,
			 size_t cap);

/* ---- Data request 0x0007 ---------------------------------------------- */

enum op_req_kind { OP_REQ_WEATHER = 1, OP_REQ_CALENDAR = 2, OP_REQ_BOTH = 3 };
enum op_req_reason {
	OP_REASON_SCHEDULE = 0,
	OP_REASON_RECONNECT = 1,
	OP_REASON_EXPIRED = 2,
	OP_REASON_USER = 3,
};

size_t op_data_request_encode(uint8_t kind, uint8_t reason, uint8_t *buf, size_t cap);

/* ---- Privacy 0x0008 --------------------------------------------------- */

struct op_privacy {
	uint8_t show_calendar_away;
};

void op_privacy_default(struct op_privacy *p);
size_t op_privacy_encode(const struct op_privacy *p, uint8_t *buf, size_t cap);
/* Sets *erase when the ERAS command is present. */
int op_privacy_apply(struct op_privacy *p, bool *erase, const uint8_t *buf, size_t len);

/* ---- Transfer control 0x0009 / data 0x000A ---------------------------- */

#define OP_BOOK_TITLE_MAX 64

enum op_xfer_op {
	OP_XFER_START = 0x01,
	OP_XFER_QUERY = 0x02,
	OP_XFER_CANCEL = 0x03,
	OP_XFER_DELETE = 0x04,
	OP_XFER_LIST = 0x05,
	OP_XFER_FINISH = 0x06,
	OP_XFER_FAST = 0x07,
	OP_XFER_PROGRESS = 0x81,
	OP_XFER_LIST_REPLY = 0x85,
	OP_XFER_RESULT = 0x86,
};

enum op_xfer_status {
	OP_XFER_OK = 0,
	OP_XFER_BAD_BLOCK = 1,
	OP_XFER_CRC_MISMATCH = 2,
	OP_XFER_NO_SPACE = 3,
	OP_XFER_UNKNOWN_BOOK = 4,
	OP_XFER_BUSY = 5,
	OP_XFER_STORAGE = 6,
};

struct op_xfer_cmd {
	uint8_t op;
	uint32_t book_id;
	uint32_t total_len;
	uint32_t crc32;
	char title[OP_BOOK_TITLE_MAX + 1];
	uint16_t start;
	uint8_t fast_on;
};

struct op_xfer_block {
	uint32_t offset;
	uint16_t crc16;
	const uint8_t *data;
	size_t len;
};

struct op_book_info {
	uint32_t book_id;
	uint32_t size;
	uint32_t position;
	char title[OP_BOOK_TITLE_MAX + 1];
};

int op_xfer_cmd_parse(struct op_xfer_cmd *c, const uint8_t *buf, size_t len);
int op_xfer_block_parse(struct op_xfer_block *b, const uint8_t *buf, size_t len);
size_t op_xfer_progress_encode(uint32_t book_id, uint8_t status, uint32_t next_offset,
			       uint8_t *buf, size_t cap);
size_t op_xfer_result_encode(uint32_t book_id, uint8_t status, uint8_t *buf, size_t cap);
/* LIST reply: header, then entries while they fit. Returns bytes, sets *count. */
size_t op_xfer_list_encode(const struct op_book_info *books, uint16_t total, uint16_t start,
			   uint8_t *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* OP_PROTO_H */

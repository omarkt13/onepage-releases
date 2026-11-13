/* OnePage service UUIDs: 4f50xxxx-6d69-6e69-8000-6f6e65706167 (protocol 2). */
#ifndef OP_UUID_H
#define OP_UUID_H

#include <zephyr/bluetooth/uuid.h>

#define OP_UUID_VAL(short_id) \
	BT_UUID_128_ENCODE(0x4f500000 | (short_id), 0x6d69, 0x6e69, 0x8000, 0x6f6e65706167)

#define OP_UUID_SERVICE_VAL OP_UUID_VAL(0x0001)

#endif

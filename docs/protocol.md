# OnePage Mini: Bluetooth protocol, version 1

This document is the byte-level contract between the firmware (`firmware/`) and
the iPhone app (`app/`). It implements section 6 of the firmware specification.
The reference encoder in `shared/tools/opproto.py` and the vectors in
`shared/test-vectors/` are normative. If this text and the vectors disagree,
the vectors win and this text gets a fix.

## 1. General rules

| Rule | Detail |
|---|---|
| Byte order | Little-endian for every integer |
| Version byte | Byte 0 of every characteristic value is the protocol version, `0x01` |
| Unknown data | A receiver ignores TLV tags it does not know, and ignores trailing bytes after the fields it knows |
| Unknown version | A receiver drops a message with a version it does not support and sets `last_error = 0x0001` |
| Strings | UTF-8, no terminator, length given by the field before them. Exception: book text, section 6 |
| Time | "Local seconds": seconds since 2000-01-01 00:00:00 **local wall-clock time**, as a `u32`. The device clock is set from the iPhone's Current Time Service, which gives local time. The device does no time-zone work |
| Security | Every OnePage characteristic needs an encrypted, bonded link (`BT_GATT_PERM_*_AUTHEN`/`_ENCRYPT`) |

### 1.1 TLV records

Characteristics marked "TLV" carry `[version u8]` followed by zero or more records:

```
[tag u8][len u8][value: len bytes]
```

A writer may send a subset of tags. A write changes only the tags it contains.

### 1.2 Checksums

| Name | Use | Definition |
|---|---|---|
| CRC-32 | Whole files, calendar blob | IEEE 802.3 (zlib `crc32`): poly `0x04C11DB7` reflected, init `0xFFFFFFFF`, final XOR `0xFFFFFFFF`. `"123456789"` gives `0xCBF43926` |
| CRC-16 | One transfer block | CRC-16/CCITT-FALSE: poly `0x1021`, init `0xFFFF`, no reflection, no final XOR. `"123456789"` gives `0x29B1` |

## 2. UUIDs

Base UUID: `4f500000-6d69-6e69-8000-6f6e65706167`. A short ID `xxxx` replaces the
two bytes `0000` in the first group: `4f50xxxx-6d69-6e69-8000-6f6e65706167`.

| Short ID | Name | Properties |
|---|---|---|
| `0001` | OnePage service | |
| `0002` | Status | read, notify |
| `0003` | Settings | read, write |
| `0004` | App filter | write, notify |
| `0005` | Home data | write |
| `0006` | Calendar sync | write, notify |
| `0007` | Data request | notify |
| `0008` | Privacy | read, write |
| `0009` | Transfer control | write, notify |
| `000A` | Transfer data | write without response |

Other services on the device: SMP (`8D53DC1D-1DB7-4CD3-868B-8A527460AA84`) for
firmware update, Battery (`0x180F`), Device Information (`0x180A`).

## 3. Advertising

| Mode | Interval | Content |
|---|---|---|
| Pairing window (2 minutes) | 100 ms | Flags; complete name `OnePage Mini`; 128-bit service UUID `4f500001-…`; manufacturer data (below) with `PAIRING` set; ANCS solicitation UUID `7905F431-B5CE-4E99-A40F-4B1E122D00D0` in the scan response |
| Bonded, reconnecting | 1 s for 30 s, then 2 s | Same, `PAIRING` clear. Directed advertising is not used, because iOS rotates its address |
| No phone, not in pairing window | Not advertising | |

Manufacturer data: `[company ID u16 = 0xFFFF][device_id: 4 bytes][flags u8]`.
`0xFFFF` is the test company ID until a Bluetooth SIG member ID is assigned.
`flags` bit 0 = `PAIRING`, bit 1 = `HAS_BOND`. `device_id` is the low 4 bytes of
the chip's `FICR.DEVICEADDR[0]`, sent as a little-endian `u32`. The NFC link and
the app show it as that `u32` in 8 upper-case hex characters (`%08X`), so the
bytes `78 56 34 12` show as `12345678`.

## 4. NFC

The built-in NFC-A Type 2 tag holds one NDEF URI record:

```
https://onepage.app/p/<device_id>
```

`onepage.app` is a placeholder until the domain is registered. It must serve
the `apple-app-site-association` file for universal links, and redirect to the
App Store when the app is not installed.

When the device detects an NFC field it wakes. If it has no bond, it opens the
2-minute pairing window. If it has a bond, it only wakes the screen.

## 5. Characteristics

### 5.1 Status `0002` (read, notify), TLV

| Tag | Type | Content |
|---|---|---|
| `0x01` | u8 | Battery, percent |
| `0x02` | u8 | Charging: 0 not charging, 1 charging, 2 full, 3 fault |
| `0x03` | string | Firmware version, for example `1.0.0` |
| `0x04` | u32 | Book storage free, KiB |
| `0x05` | u32 | Book storage total, KiB |
| `0x06` | u16 | Last error code (section 7), 0 if none |
| `0x07` | u32 | Stored calendar version, 0 if none |
| `0x08` | u32 | Calendar synced at, local seconds, 0 if never |
| `0x09` | u8 | Hardware revision |

The device notifies Status on connection, on a change of battery percent or
charging state, and after each error.

### 5.2 Settings `0003` (read, write), TLV

| Tag | Type | Content | Default |
|---|---|---|---|
| `0x01` | u8 | Quiet hours on (0/1) | 0 |
| `0x02` | u16 | Quiet start, minutes after midnight | 1320 (22:00) |
| `0x03` | u16 | Quiet end, minutes after midnight | 420 (07:00) |
| `0x04` | u8 | Refresh mode: 0 quality, 1 balanced, 2 fast | 1 |
| `0x05` | u8 | Clock: 0 = 24 hour, 1 = 12 hour | 0 |
| `0x06` | 2 bytes | Language, ISO 639-1, for example `en` | `en` |
| `0x07` | u8 | Alert strength: 0 off, 1 low, 2 medium, 3 high | 2 |

### 5.3 App filter `0004` (write, notify)

The device keeps up to 64 entries. It adds an entry with mode `SHOW` the first
time an app identifier arrives in a notification. When full, it drops the
oldest entry that is still `SHOW` with no flags and pattern 0.

Entry encoding:

```
[mode u8][pattern u8][flags u8][id_len u8][id: id_len bytes]
```

| Field | Values |
|---|---|
| mode | 0 show, 1 silent (on the list, no LED or vibration), 2 hide (not stored, not shown) |
| pattern | 0 default, 1 single, 2 double, 3 long, 4 none |
| flags | bit 0 `TITLE_ONLY`: show the app name and time, not the text |
| id | iOS bundle identifier, at most 64 bytes |

Operations, written by the app:

| Op | Layout | Device reply (notify) |
|---|---|---|
| `0x01` LIST | `[ver][0x01][start u16]` | `[ver][0x81][total u16][start u16][count u8][entry …]`, as many entries as fit in ATT MTU − 3 |
| `0x02` SET | `[ver][0x02][entry]` | `[ver][0x82][result u8]` |
| `0x03` REMOVE | `[ver][0x03][id_len u8][id]` | `[ver][0x83][result u8]` |

`result` is 0 for success, or an error code's low byte (section 7). SET on an
unknown identifier adds it.

### 5.4 Home data `0005` (write), TLV

| Tag | Type | Content |
|---|---|---|
| `0x01` | i16 | Temperature now, tenths of a degree Celsius |
| `0x02` | i16 | Today's high, tenths of °C |
| `0x03` | i16 | Today's low, tenths of °C |
| `0x04` | u8 | Condition (table below) |
| `0x05` | u32 | Valid until, local seconds |
| `0x06` | string | Place name, at most 24 bytes |
| `0x07` | u8 | Display unit: 0 °C, 1 °F |

Condition codes: 0 unknown, 1 clear, 2 partly cloudy, 3 cloudy, 4 fog,
5 drizzle, 6 rain, 7 heavy rain, 8 snow, 9 sleet, 10 thunderstorm, 11 wind,
12 hot, 13 cold.

The device hides weather when its clock passes "valid until".

### 5.5 Calendar sync `0006` (write, notify)

The app sends the whole week as one blob in three steps. The device keeps its
current copy until COMMIT passes the length and CRC-32 checks, then replaces it
in one step.

| Op | Layout |
|---|---|
| `0x01` BEGIN | `[ver][0x01][cal_version u32][total_len u32][crc32 u32]` |
| `0x02` DATA | `[ver][0x02][offset u32][bytes …]` |
| `0x03` COMMIT | `[ver][0x03][cal_version u32]` |
| `0x04` CLEAR | `[ver][0x04]`: erase the stored calendar |

Device reply (notify) after BEGIN, after COMMIT, after CLEAR, and after any
DATA that fails:

```
[ver][0x80][status u8][cal_version u32][next_offset u32]
```

`status`: 0 OK, 1 out-of-order offset (app restarts from `next_offset`),
2 CRC or length mismatch at COMMIT, 3 too large, 4 bad blob content, 5 no BEGIN.

`total_len` is at most 16384. DATA chunks may be any size up to ATT MTU − 3 − 6.

Blob layout:

```
[generated_at u32][count u16]
count × event:
  [start u32][end u32][flags u8][title_len u8][title][place_len u8][place]
```

| Field | Rule |
|---|---|
| generated_at | Local seconds when the app built the blob. Shown as "Synced" |
| count | At most 100 |
| start, end | Local seconds. For all-day events: midnight at the start of the first day, and midnight after the last day |
| flags | bit 0 all-day |
| title | At most 80 characters (Unicode scalar values) and 160 bytes. Longer text is cut to 79 characters plus `…` (U+2026) |
| place | At most 60 characters and 120 bytes, cut the same way. Empty if none |

Events are in start order. The app includes events that started before now but
end after now.

### 5.6 Data request `0007` (notify)

```
[ver][kind u8][reason u8]
```

| kind | Meaning |
|---|---|
| 1 | Refresh weather |
| 2 | Refresh calendar |
| 3 | Refresh both |

| reason | Meaning |
|---|---|
| 0 | Schedule (hourly weather; calendar at midnight and every 6 hours) |
| 1 | Reconnected after the phone was away |
| 2 | Stored data expired |
| 3 | User asked on the device |

The app must answer within the few seconds iOS gives it in the background:
Home data for weather, a full calendar sync for calendar.

### 5.7 Privacy `0008` (read, write), TLV

| Tag | Type | Content | Default |
|---|---|---|---|
| `0x01` | u8 | Show calendar when the phone is away (0/1) | 1 |
| `0xE0` | 4 bytes | Write only. `ERAS` (`0x45 0x52 0x41 0x53`) erases calendar, weather, settings, app filter and books, removes the bond, and restarts | |

### 5.8 Transfer control `0009` (write, notify)

| Op | Layout | Meaning |
|---|---|---|
| `0x01` START | `[ver][0x01][book_id u32][total_len u32][crc32 u32][title_len u8][title]` | Start a new book, or continue one with the same id, length and CRC |
| `0x02` QUERY | `[ver][0x02][book_id u32]` | Ask for the resume offset |
| `0x03` CANCEL | `[ver][0x03][book_id u32]` | Stop and discard a partial book |
| `0x04` DELETE | `[ver][0x04][book_id u32]` | Delete a stored book |
| `0x05` LIST | `[ver][0x05][start u16]` | List books |
| `0x06` FINISH | `[ver][0x06][book_id u32]` | Check whole-file CRC-32 and store |
| `0x07` FAST | `[ver][0x07][on u8]` | Ask for the shortest connection interval (on = 1) or return to idle (0). Valid at any time, also before a calendar sync. The device returns to idle by itself 30 s after the last FAST or transfer write |

Device replies (notify):

| Reply | Layout |
|---|---|
| `0x81` PROGRESS | `[ver][0x81][book_id u32][status u8][next_offset u32]` |
| `0x85` LIST | `[ver][0x85][total u16][start u16][count u8]` then per book `[book_id u32][size u32][position u32][title_len u8][title]` |
| `0x86` RESULT | `[ver][0x86][book_id u32][status u8]` |

`status`: 0 OK, 1 out-of-order or bad block (resume from `next_offset`),
2 whole-file CRC mismatch, 3 no space, 4 unknown book, 5 busy (another book
in progress), 6 storage error.

The device sends PROGRESS after START, after QUERY, after every 32 accepted
blocks, and at the first rejected block after a run of good ones.
`position` in LIST is the reader's byte offset; the app shows
`position / size` as progress.

### 5.9 Transfer data `000A` (write without response)

```
[ver][offset u32][crc16 u16][data …]
```

`crc16` covers `data` only. The device accepts the block only if `offset`
equals its expected offset and the CRC matches. It drops any other block and
reports the expected offset in its next PROGRESS. Block data size is at most
ATT MTU − 3 − 7.

## 6. Book file (`.opb`)

The app converts EPUB and text files on the phone. The device stores the file
as received and never parses EPUB.

```
Header, 16 bytes:
  magic      4 bytes  "OPB1"
  version    u8       1
  charset    u8       1 = Windows-1252
  chapters   u16      number of chapter entries
  text_off   u32      offset of the text from the start of the file
  text_len   u32      length of the text in bytes
Metadata:
  title_len u8, title (UTF-8, at most 64 bytes)
  author_len u8, author (UTF-8, at most 64 bytes)
Chapter table, `chapters` times:
  text_offset u32     offset within the text
  name_len u8, name (UTF-8, at most 48 bytes)
Text, text_len bytes, Windows-1252:
  0x0A ends a paragraph. No other control characters.
```

The app maps characters outside Windows-1252 to near equivalents (for example
non-breaking hyphen to `-`) and otherwise to `?`. It drops images.

## 7. Error codes (Status tag `0x06`)

| Code | Meaning |
|---|---|
| `0x0000` | None |
| `0x0001` | Unsupported protocol version |
| `0x0002` | Malformed message |
| `0x0003` | Storage full |
| `0x0004` | Storage fault |
| `0x0005` | Calendar rejected |
| `0x0006` | Book transfer failed |
| `0x0007` | Display fault |
| `0x0008` | Touch controller fault |

## 8. Timing the device keeps

| Event | Rule |
|---|---|
| Weather request | Every 60 minutes while connected, and at reconnection |
| Calendar request | At 00:00, 06:00, 12:00, 18:00 local, and at reconnection |
| Phone away | Link down for 2 minutes: locked Home, notifications and music erased from RAM |
| Phone away 7 days | Calendar and weather erased |
| Connection interval | Idle: request 1815–1965 ms, latency 0, supervision timeout 6 s. This meets Apple's accessory rules: max interval × (latency + 1) ≤ 2 s, min + 15 ms ≤ max, and 3 × max < timeout. Transfer or update: 15–30 ms |

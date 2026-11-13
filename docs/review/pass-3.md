# Review pass 3: firmware and app, byte by byte

Area (c). I compared the firmware (`firmware/src/core/op_proto.c`, `firmware/src/ble/*`,
`firmware/src/core/{transfer,calendar}.c`) with the app (`app/OnePageKit/Sources/OnePageKit/*`,
`app/OnePageMini/{BLE,Services}/*`) for every characteristic in `docs/protocol.md`.

Result: 0 high, 3 medium, 4 low. 5 fixed, 2 open (low).

## Medium

| # | File | Problem | Fix | State |
|---|---|---|---|---|
| M1 | `app/OnePageMini/BLE/DeviceManager.swift`, `Services/{CalendarService,BookLibrary,FirmwareUpdater}.swift` | Calendar sync, book transfer and firmware update each send FAST on at the start and FAST off at the end. They can overlap: a calendar sync that ends during a book transfer sends FAST off, and the firmware goes to the 2 s idle interval. Block writes only refresh the firmware's 30 s off-timer; they do not turn fast mode on again, so the rest of the book goes at idle speed | `DeviceManager.acquireFastLink()` / `releaseFastLink()` count users. FAST off goes out only when the last user stops. The updater takes one hold and repeats a plain FAST on every 10 s | Fixed |
| M2 | `app/OnePageKit/Sources/OnePageKit/Calendar.swift` `CalendarBlob.encode` | 100 events with the longest title and place make a 29,106-byte blob. The firmware takes at most 16,384 (`CAL_MAX_BLOB`). The app then refused the whole week, so the device kept an old calendar | The encoder leaves out the latest events until the blob fits. Same rule in the reference encoder `shared/tools/opproto.py`. New vector `calendar_cap` (56 of 100 events, 16,302 bytes). New tests: `testCalendarCap` (Swift) and `calendar_cap` (C, checks that the firmware takes the largest blob whole) | Fixed |
| M3 | `firmware/src/core/transfer.c` `start`, `firmware/src/storage/books.c` `io_open` | Open item 7. A START for book B while book A was in progress got status 5 "busy". The app cannot clear A if the user deleted it on the phone. Also, a partial book left after a dropped link stayed in flash for ever when the next START named another book | Firmware decides: the phone is the only writer, so the newest START wins. `start` discards the active partial book. `io_open` deletes every partial book (`.prt`/`.pin`) with another id. Status 5 is now reserved. Host test changed from "busy" to "the newest START wins" | Fixed |

## Low

| # | File | Problem | Fix | State |
|---|---|---|---|---|
| L1 | `firmware/src/ble/op_service.c` `xfer_thread` | Commands were done before queued blocks. A QUERY sent after the last block could report an offset up to 12 blocks old, and the app sent those blocks again. A FINISH could fail in the same way | `drain_blocks(false)` writes the blocks queued before a QUERY or FINISH first. The main loop still stops between blocks when a command waits, so CANCEL is not delayed | Fixed |
| L2 | `app/OnePageMini/Services/CalendarService.swift` `send` | The BEGIN reply carries `next_offset` for a staged copy, but the app always started at 0. The device answered "out of order", which used one of 3 restarts | Start at the BEGIN reply's `next_offset` | Fixed |
| L3 | `app/OnePageMini/Services/CalendarService.swift` `send` | DATA failures are notified from another firmware thread, so one can arrive after the app's last DATA write returns. The app then reads it as the COMMIT reply and reports a failure. The next sync (at most 6 hours, or on app open) repairs it | Before COMMIT, wait about 200 ms and drain the mailbox, or match the COMMIT reply by its `next_offset == total_len`. Check on the bench first | Open |
| L4 | `firmware/src/ble/op_service.c` (DELETE), `firmware/src/storage/books.c` | DELETE for the book that is now receiving deletes its files, but `xfer.active` stays true with a file handle on the deleted file. Later blocks fail and the transfer ends with "storage error" | DELETE of the active id should call `xfer_command(CANCEL)` first. The app never sends this today | Open |

## Checked and conforming

- UUID base and short IDs (`firmware/src/ble/op_uuid.h`, `OnePageUUID`).
- Version byte, TLV `[tag][len][value]`, unknown tags skipped, partial writes change only the tags present: Status, Settings, Home data, Privacy on both sides.
- Little-endian integers everywhere, `i16` tenths of °C, `u32` local seconds.
- App filter: entry layout, LIST/SET/REMOVE opcodes `0x01–0x03`, replies `0x81–0x83`, LIST reply sized to ATT MTU − 3 (`ble_att_payload`).
- Calendar: BEGIN/DATA/COMMIT/CLEAR, ACK `0x80` with status 0–5. Chunk size `maxWriteLength − 6` fits one ATT write, so writes with response never use prepared writes (which the firmware refuses with `INVALID_OFFSET`).
- Transfer: START/QUERY/CANCEL/DELETE/LIST/FINISH/FAST, PROGRESS `0x81`, LIST `0x85`, RESULT `0x86` (status at byte 6). Block `[ver][offset u32][crc16 u16][data]`, block size `maxWriteLength − 7`.
- ATT MTU: firmware RX buffers give at most MTU 247, so the app's `maximumWriteValueLength(.withoutResponse)` is at most 244, equal to the firmware's `BLOCK_MAX`.
- Text limits: title 80 chars / 160 bytes, place 60 / 120, weather place 24 bytes (firmware buffer 25), book title 64 bytes (firmware 64 + 1), app ID 64.
- Data request `[ver][kind][reason]`, kinds 1–3; the app reads a missing reason as nil.
- Advertising: company `0xFFFF` LE, device ID `FICR.DEVICEADDR[0]` LE, flags bit 0 PAIRING, bit 1 HAS_BOND. Scan response 27 of 31 bytes.
- NFC: `NFC_URI_HTTPS` prefix + `onepage.app/p/%08X`; `PairingLink.deviceID` accepts exactly that form.
- Idle interval 1452–1572 units (1815–1965 ms), latency 0, timeout 6 s.

## Checks

| Check | Result |
|---|---|
| Host tests | 2 of 2 pass. Vectors: 12 cases, 209 checks. Behaviour: 9 cases, 237 checks |
| `nrf54l15dk/nrf54l15/cpuapp` with MCUboot | Pass, no warnings from `firmware/src`. App: 392,016 B flash, 221,692 B RAM (84.6%) |
| `onepage_mini/nrf54l15/cpuapp` with MCUboot | Pass, no warnings from `firmware/src`. App: 393,044 B flash, 221,668 B RAM (84.6%) |
| `opproto.py` regenerates `vectors.json` | No difference |
| Swift | Not compiled (no Swift toolchain here) |

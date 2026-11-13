# Review pass 9: firmware and app byte by byte (rotation 2)

Area (c). Scope: every characteristic in `docs/protocol.md`, with a focus on the
behaviour that passes 3 to 8 changed: FAST reference counting, the oversize week,
START replaces a partial book, QUERY and FINISH order, reconnection data
requests, `xfer_forget()`, and the fast link during an update.

Result: 0 high, 2 medium, 3 low. 2 fixed. 3 low recorded.

## Checked with no finding

| Item | Firmware | App |
|---|---|---|
| Block size against MTU | `BLOCK_MAX` 244 = ATT MTU 247 − 3 (`op_service.c:30`, `prj.conf:50`) | Block = `maximumWriteValueLength(.withoutResponse)` − 7 (`Transfer.swift:132`) |
| Calendar DATA size | Any length up to the staged size | Chunk = write-without-response length − 6, so no long (prepared) writes, which the firmware refuses with `offset != 0` |
| Calendar limit | `CAL_MAX_BLOB` 16384, 100 events (`calendar.h:17`) | `maxBlobBytes` 16384, title 80/160, place 60/120 (`Calendar.swift:70`) |
| FAST | Valid at any time; off cancels the 30 s timer; SMP chunks turn it on again (`op_service.c:92`, `:341`) | Hold count: FAST off only when the last of calendar, book and update stops (`DeviceManager.swift:430`) |
| START replaces a partial | `transfer.c:21` | `TransferPlan.apply` follows the device back to 0 |
| QUERY and FINISH order | Queued blocks are written first (`op_service.c:485`) | Takes the largest offset after QUERY; FINISH waits for RESULT |
| DELETE during transfer | `xfer_forget()` then delete (`op_service.c:488`) | Cancels its own send, sends CANCEL, then DELETE (`BookLibrary.swift:119`) |
| Device ID | `%08X` of LE u32 `FICR.DEVICEADDR[0]` (`ble_core.c:532`) | Same (`DeviceLink.swift:27`) |
| Reconnection request | kind 3, reason 1 at link ready (`alerts.c:44`) | Ignored; `onReady` sends calendar and weather |

## Findings

### M1. Every routine PROGRESS made the app resend all blocks in flight (fixed)

- **Files:** `app/OnePageMini/Services/BookLibrary.swift:263`, `app/OnePageKit/Sources/OnePageKit/Transfer.swift:152`
- **Problem:** the device sends PROGRESS (status OK) every 32 accepted blocks. When it arrives, the app has already queued more blocks, so the reported offset is behind `nextOffset`. `apply(deviceOffset:)` treats any lower offset as a rewind (pass 3 made the device the authority). So at each report the app went back and sent every block in flight again. The device refused each duplicate (offset not expected), sent one bad-block PROGRESS with its real offset, and the app jumped forward. The transfer still finished, but sent many blocks twice, which works against the 30–80 KB/s target in the app spec (4.3).
- **Fix:** new `TransferPlan.confirm(deviceOffset:)`. It moves `confirmedOffset` forward only and never rewinds. The send loop uses it for status OK. Bad-block PROGRESS, and the replies to START and QUERY, still use `apply`, which can rewind. New test `testRoutineProgressConfirmsWithoutRewind` in `CoverageTests.swift`.

### M2. A background reconnection had no background time (fixed)

- **File:** `app/OnePageMini/App/AppModel.swift:50`
- **Problem:** pass 8 made the app ignore the reconnection data request, because `deviceBecameReady()` already sends the calendar and the weather. But only the data-request path asked iOS for background time (`BackgroundActivity`). A reconnection usually wakes the app in the background, where iOS gives about 10 s to a Bluetooth event. A WeatherKit call plus a full week at the slow-to-fast switch can take longer, so iOS could suspend the app during the sync.
- **Fix:** `deviceBecameReady()` now holds a `BackgroundActivity` for its whole run.

### L1. Calendar resume never takes effect (recorded)

- **File:** `app/OnePageMini/Services/CalendarService.swift:114`
- **Problem:** the device resumes a staged calendar only for the same version, length and CRC (`calendar.c:106`). The app uses the current time as the version and as `generated_at`, so each sync is a new blob and starts from 0. Resume works only inside one `send` call. A week is at most 16 KB, so the cost is small.
- **Fix, if wanted:** keep the last unfinished blob and version in `CalendarService` and send it again when the events have not changed.

### L2. Late calendar DATA failures can count as several restarts (recorded with open item 9)

- **File:** `app/OnePageMini/Services/CalendarService.swift:164`
- **Problem:** the device answers each failed DATA. The app reads answers only between writes, so it can send more DATA before the first failure arrives. Each later DATA fails too, and each failure is one restart in the app's limit of 3. One real gap can use all 3 and fail the sync. The next sync repairs it. This is the same mechanism as open item 9.
- **Fix:** with open item 9: after a restart, drop failure replies whose `next_offset` equals the offset already restarted from.

### L3. CANCEL reply can be read as the DELETE reply (recorded)

- **File:** `app/OnePageMini/Services/BookLibrary.swift:128`
- **Problem:** when the user deletes the book being sent, the app sends CANCEL and then clears the mailbox and sends DELETE. The reply to CANCEL can arrive after the clear and be taken as the DELETE reply. The code accepts any reply that is not an error RESULT, so the effect is only that it does not wait for the real DELETE result. No wrong state results, because the book list is read again.
- **Fix, if wanted:** wait for a RESULT whose book ID and position follow the DELETE.

## Checks

| Check | Result |
|---|---|
| Host tests | 3 of 3 suites pass |
| Firmware builds | Not run: no firmware or shared files changed |
| `vectors.json` | Regenerates with no difference |
| Swift | Not compiled (no compiler here). The new code uses only types already in the files |

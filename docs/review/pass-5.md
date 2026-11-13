# Review pass 5: firmware robustness

Area (e). Scope: error paths and return codes, power states and wake sources,
RAM and thread stacks, concurrency between the Bluetooth RX thread, the work
queues and the application thread, privacy rules, watchdog, battery limits,
storage failures, and the two items queued for this pass (open items 8 and 10).

## Findings, most severe first

| # | Severity | File | Problem | Fix | State |
|---|---|---|---|---|---|
| M1 | Medium | `firmware/prj.conf`, `src/app.c` | No watchdog. A stuck application thread (a hung SPI or I²C transfer, a littlefs fault, a loop such as L1 below) leaves the device frozen until the battery is flat. The last image stays on the e-ink panel, which can be a private message | Enable `wdt31` on both boards, `CONFIG_WATCHDOG=y`. The application thread installs a 10-minute channel with `WDT_FLAG_RESET_SOC` and feeds it on every loop. The loop wakes at least every 5 minutes (battery poll). The counter pauses only when a debugger halts the CPU | Fixed |
| M2 | Medium | `src/ble/op_service.c` `send_book_list` | LIST ran in the Bluetooth RX thread (3072-byte stack) with 16 × `op_book_info` (about 1.3 KB) and a 244-byte reply buffer on the stack, under the GATT call chain. Likely stack overflow on a full library | The two buffers are now `static`. Only the RX thread calls this function, and there is one connection | Fixed |
| M3 | Medium | `src/ble/op_service.c`, `src/core/transfer.c` (open item 10) | DELETE of the book now being received removed its files, but the transfer stayed active and wrote the next blocks into a deleted file | New portable `xfer_forget()`: if the id is the active transfer, discard the partial file and stop. The xfer thread calls it before `books_delete()`. DELETE of a partial book now answers OK. Host test `transfer_forget` | Fixed |
| M4 | Medium | `src/app.c` | `EVT_BOOKS` was not handled. If the app deleted the book open in the reader, the reader kept reading a deleted file | On `EVT_BOOKS`, if the open book is no longer in the library, close the reader and show Library | Fixed |
| L1 | Low | `src/storage/books.c` `books_erase_all` | `while (count) books_delete(...)`: if a file cannot be removed, `count` never falls and the loop never ends. Erase-and-reset would hang | Bounded loop; returns `-EIO` if books remain. With M1, a hang now also ends in a reset | Fixed |
| L2 | Low | `src/ble/op_service.c` (open item 8) | The firmware did not ask for the fast interval on SMP update traffic. Only the app's FAST command kept an update fast | `CONFIG_MCUMGR_GRP_IMG_UPLOAD_CHECK_HOOK`; an observe-only callback on each upload chunk calls `op_service_fast_keepalive()`, which asks for the fast interval once and keeps it 30 s after the last chunk. New `ble_is_fast()` stops a parameter request on every chunk | Fixed |
| L3 | Low | `src/ble/ble_core.c` `ble_set_fast` | `fast_mode` is written from the RX thread, the system work queue and the xfer thread without a lock. Worst case is one extra or one missed parameter request, which the 30 s keepalive or the next request corrects | None. Recorded only | Open, accepted |
| L4 | Low | `src/storage/books.c` | Between a DELETE in the xfer thread and `EVT_BOOKS` in the application thread, a page turn can read the deleted file. littlefs keeps an open file readable, so the effect is at most one page from the old book | None now. Check on the bench with the delete test in `docs/TESTING.md` | Open |
| L5 | Low | thread stacks | Stack sizes are set by reading only: main 4096, RX 3072, xfer 3072, notify 1536, system work queue 2048. RAM is at 85 % (223 KB of 256 KB), so they cannot grow much | Bench: build with `CONFIG_THREAD_ANALYZER=y` and `CONFIG_THREAD_ANALYZER_AUTO=y`, run a book transfer, a full library LIST, a firmware update and a full refresh, and record the peak use of each thread | Open, bench |

## Checked with no finding

- Notification and music text: held only in RAM (`notif_list`, `g_app.music`). No `settings_save`, `fs_write` or `zms_write` in `ancs.c`, `ams.c` or `notif_list.c`. The 2-minute lock clears both and forces a full refresh.
- 7-day rule: survives a restart (pass 1); erases calendar and weather and saves the empty calendar.
- Battery: 10 % warning once per discharge, 3 % draws "Charge me" and enters System OFF with button and charger wake. An absent fuel gauge returns an error and does not cause a power-off.
- Erase: personal data, books and bond, then a cold restart.
- `apply_privacy` is called with the state mutex held; Zephyr mutexes are recursive for the owner, so the nested lock is safe.
- `store_save_away` writes only when the hour count changes.

## Checks

| Check | Result |
|---|---|
| Host tests | 2 of 2 suites pass. Behaviour suite has 10 cases (new: `transfer_forget`) |
| `nrf54l15dk/nrf54l15/cpuapp` with MCUboot | Pass. App 394,012 B flash (55.1 %), 223,332 B RAM (85.2 %). No warnings from `firmware/src` |
| `onepage_mini/nrf54l15/cpuapp` with MCUboot | Pass. App 395,024 B flash (55.3 %), 223,300 B RAM (85.2 %). No warnings from `firmware/src` |
| `wdt_nrf`, `smp_chunk_cb`, `xfer_forget` linked in both images | Yes |
| `opproto.py` regenerates `vectors.json` | No difference |

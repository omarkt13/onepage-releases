# Review pass 11: firmware robustness (area e, rotation 2)

Scope: error paths, power states and wake sources, RAM and stacks, thread
concurrency, privacy rules and storage failures, with extra attention to the
firmware code that passes 5 to 9 changed (watchdog, static list buffers,
`xfer_forget`, reader close on delete, `app_power_off`, `disc_started`, the
update fast link, `cal_current_nth`, image confirm after Bluetooth starts,
bench away-erase option). Items already open in `STATUS.md` are not repeated.

The pass 5 to 9 changes read correctly. No regression found in them.

## Findings

| # | Severity | File | Problem | Fix |
|---|---|---|---|---|
| M1 | Medium | `firmware/src/app.c` `battery_check()` | One gauge reading of 3 % or less switched the device off. The MAX17048 model for the 4.35 V cell is still a placeholder, so the percentage can be wrong, and at start-up `app_run()` and the first `periodic()` read the gauge twice within milliseconds. A wrong reading leaves the device off until the charger is connected | Applied. New portable `core/battery_policy.c`: "low" needs 3 % or less **and** 3.55 V or less (when the voltage is known), or 3.30 V or less at any percentage. Off needs the low state to last at least 60 s over two or more readings. The warning rule moved there unchanged. New host test `battery_policy` |
| M2 | Medium | `firmware/src/storage/books.c` `books_erase_all()` | Erase and reset deleted only indexed books. A partial transfer (`.prt` and `.pin`) stayed on the external flash after the restart. That is book text left on a device that the owner erased to sell or give away (spec 7, "Erase and reset") | Applied. `unlink_dir_all()` removes every file in `/lfs/b`, bounded so a file that cannot be removed cannot hang the device. New bench test T-AWAY-8 |
| L1 | Low | `firmware/src/ble/op_service.c` `xfer_thread()` | The SUSPEND command queued at disconnect called `ble_set_fast(false)`. If the xfer thread was busy (for example a whole-file CRC) and the phone reconnected and sent FAST first, SUSPEND turned FAST off 5 s later in the new connection | Applied. SUSPEND no longer touches the link state. The disconnect already clears it |
| L2 | Low | `firmware/src/drivers/charger.c` `charger_prepare_poweroff()` | Power menu > Power off while charging armed a level wake on STAT1, which was already low. The device restarted at once | Applied. If STAT1 is already low, only the power button wakes the device. New bench test T-PWR-9 |
| L3 | Low | `firmware/src/storage/store.c` `store_erase_personal()` | The saved time-away record (`op/away`) survived an erase | Applied. Erased with the other personal data |
| L4 | Low | `firmware/src/drivers/charger.c` header comment | The comment called STAT1/STAT2 L/L a fault. The MCP73833 datasheet shows faults as H/H (same as no input) and L/L as test mode. The code is correct: a fault on battery counts as "not charging" | Applied. Comment corrected |

## Checked and found correct

- Watchdog: 10-minute window; the application loop wakes at least every 5 minutes. The only long blocks on that thread are a full refresh, a calendar save to RRAM and an erase, all well under the window. The watchdog is started after the image confirm.
- `send_book_list()` static buffers: only the Bluetooth RX thread calls it, and `CONFIG_BT_MAX_CONN=1`.
- `xfer_forget()` then `books_delete()` run in the xfer thread, in order, so no block lands in a deleted file.
- `EVT_BOOKS` reader close and `app_power_off()` save or drop the reading position correctly.
- `disc_started` is cleared in `disconnected()`, so a new connection always discovers again.
- Image confirm: an image is kept only when `bt_enable` succeeded; otherwise the device restarts and MCUboot reverts.
- Privacy: notification text and music stay in RAM. `apply_privacy()` takes the state mutex recursively from `EVT_LINK_READY` (Zephyr mutexes allow this for the owner thread).
- Storage failures: `store_init()` failure sets `OP_ERR_STORAGE_FAULT`; a damaged calendar is dropped with its CRC check; littlefs mount failure leaves the device working without books.
- RAM: 85.2 % on both boards, unchanged by this pass.

## Verification

| Check | Result |
|---|---|
| Host tests | 3 of 3 suites pass. Behaviour suite now has 11 cases |
| `nrf54l15dk/nrf54l15/cpuapp` with MCUboot | Builds, no warnings from `firmware/src`. App 394,604 B flash, 223,340 B RAM (85.2 %) |
| `onepage_mini/nrf54l15/cpuapp` with MCUboot | Builds, no warnings from `firmware/src`. App 395,616 B flash, 223,308 B RAM (85.2 %) |
| `opproto.py` regenerates `vectors.json` | No difference |

Result: not clean (2 medium findings).

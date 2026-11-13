# Review loop status

Goal: the repository is complete and ready for bench testing.

## Stop rule

The loop stops when two full rotations in a row (12 passes) have no high or medium finding and
all checks pass, or after 12 passes in total. A pass is "clean" when it finds no high or medium
issue and all checks pass.

| Item | Value |
|---|---|
| Last pass | 5 |
| Next area | (f) test coverage and `docs/TESTING.md` |
| Consecutive clean passes | 0 |
| Passes left before the 12-pass limit | 7 |

Rotation: (a) firmware against specs, (b) app against specs, (c) firmware and app byte by byte,
(d) Swift compile risk, (e) firmware robustness, (f) test coverage and `docs/TESTING.md`.

## Pass log

| Pass | Area | High | Medium | Low | Fixed | Clean | Commit |
|---|---|---|---|---|---|---|---|
| 1 | (a) firmware against specs | 0 | 2 | 4 | 3 | No | 88136f1 |
| 2 | (b) app against specs | 0 | 3 | 4 | 5 | No | 48ac5b7 |
| 3 | (c) firmware and app byte by byte | 0 | 3 | 4 | 5 | No | 319edc8 |
| 4 | (d) Swift compile risk | 0 | 2 | 6 | 8 | No | 2bb750f |
| 5 | (e) firmware robustness | 0 | 4 | 5 | 6 | No | 3b28eab |

## Open items

| # | From pass | Item | Needs |
|---|---|---|---|
| 1 | 1 | LED blinks from a kernel timer, not a hardware timer | Power measurement at firmware step 4 |
| 2 | 1 | Display supply stays on for 2 minutes after a refresh | Power measurement at firmware step 4 |
| 3 | 1 | A full block queue holds the Bluetooth RX thread for up to 200 ms during a book transfer | Bench test: notification delay during a transfer |
| 4 | before loop | All pins and crystal capacitors on the custom board (`TODO(pcb)`) | PCB design |
| 5 | before loop | Display commands and waveforms, IQS7222A set-up, S25HL512T sector map, MAX17048 4.35 V model, LRA voltages (`TODO(datasheet)`) | Datasheets |
| 6 | before loop | The app was never compiled. Pass 4 read every Swift file and fixed the compile risks it found (`docs/review/pass-4.md`). A real build, `swift test` and the McuManager 1.6 API check still need Xcode | A Mac with Xcode 15.4 or later |
| 9 | 3 | A late calendar DATA failure can be read as the COMMIT reply; the sync fails and the next sync repairs it (pass-3 L3) | Bench test, then fix in the app |
| 11 | 5 | `ble_set_fast` state is shared by three threads without a lock (pass-5 L3) | Accepted; watch on the bench |
| 12 | 5 | A page turn can read a just-deleted book once before the reader closes (pass-5 L4) | Bench test |
| 13 | 5 | Thread stack sizes set by reading only; RAM at 85 % (pass-5 L5) | Bench run with `CONFIG_THREAD_ANALYZER` |

## Decisions needed from the user

| # | Decision |
|---|---|
| 1 | MCU module: AN54LV-15 (component list) or AN54LQ-15 (spec) |
| 2 | Domain for the NFC link and universal links (now `onepage.app`) |
| 3 | Bluetooth SIG company ID (now the test ID `0xFFFF`) |
| 4 | Firmware signing keys (MCUboot now uses the development key) and the update manifest URL |
| 5 | Calendar visible by default when the phone is away (now: on) |

## Resolved

| Item | Resolution |
|---|---|
| Firmware does not ask for the fast interval on SMP traffic (open item 8) | Pass 5: upload-chunk hook keeps the link fast |
| DELETE of the book now receiving (open item 10) | Pass 5: `xfer_forget()` stops the transfer first; the reader closes if its book is deleted |
| Interrupted book transfer blocks the next book with "busy" (open item 7) | Pass 3: the newest START wins; the firmware discards the other partial book |
| Where the source goes | The user chose to push to this repository, `omarkt13/onepage-releases`, branch `claude/magical-edison-9m5j8x` |

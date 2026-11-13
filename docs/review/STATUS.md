# Review loop status

Goal: the repository is complete and ready for bench testing.

## Stop rule

The loop stops when two full rotations in a row (12 passes) have no high or medium finding and
all checks pass, or after 12 passes in total. A pass is "clean" when it finds no high or medium
issue and all checks pass.

| Item | Value |
|---|---|
| Last pass | 8 |
| Next area | (c) firmware and app byte by byte (rotation 2) |
| Consecutive clean passes | 0 |
| Passes left before the 12-pass limit | 4 |

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
| 6 | (f) test coverage and `docs/TESTING.md` | 0 | 4 | 5 | 9 | No | 17a68cb |
| 7 | (a) firmware against specs | 0 | 1 | 3 | 4 | No | eb38ef1 |
| 8 | (b) app against specs | 0 | 1 | 4 | 5 | No | 975d542 |

## Open items

| # | From pass | Item | Needs |
|---|---|---|---|
| 1 | 1 | LED blinks from a kernel timer, not a hardware timer | Power measurement: `docs/TESTING.md` T-PWR-3 |
| 2 | 1 | Display supply stays on for 2 minutes after a refresh | Power measurement: T-PWR-4 |
| 3 | 1 | A full block queue holds the Bluetooth RX thread for up to 200 ms during a book transfer | Bench test T-NOT-8 |
| 4 | before loop | All pins and crystal capacitors on the custom board (`TODO(pcb)`) | PCB design |
| 5 | before loop | Display commands and waveforms, IQS7222A set-up, S25HL512T sector map, MAX17048 4.35 V model, LRA voltages (`TODO(datasheet)`) | Datasheets |
| 6 | before loop | The app was never compiled. Pass 4 read every Swift file and fixed the compile risks it found (`docs/review/pass-4.md`). A real build, `swift test` and the McuManager 1.6 API check still need Xcode | A Mac with Xcode 15.4 or later: `docs/TESTING.md` steps A1–A8 |
| 9 | 3 | A late calendar DATA failure can be read as the COMMIT reply; the sync fails and the next sync repairs it (pass-3 L3) | Bench test T-CAL-7, then fix in the app |
| 11 | 5 | `ble_set_fast` state is shared by three threads without a lock (pass-5 L3) | Accepted; bench test T-PWR-7 |
| 12 | 5 | A page turn can read a just-deleted book once before the reader closes (pass-5 L4) | Bench test T-BOOK-6 |
| 13 | 5 | Thread stack sizes set by reading only; RAM at 85 % (pass-5 L5) | Bench test T-PWR-6 (`build-stack`) |
| 14 | 6 | Pass 6 added Swift tests (`CoverageTests.swift`) that have not run | Step A2 in `docs/TESTING.md` |
| 15 | 6 | The manifest public key in the app is empty, so the app refuses every update until a release key is made | `make_manifest.py keygen`, step A7 (with user decision 4) |

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
| No bench test plan; 7-day rule not testable on the bench; no manifest signing tool | Pass 6: `docs/TESTING.md`, bench Kconfig `ONEPAGE_TEST_AWAY_ERASE_MIN`, `shared/tools/make_manifest.py` |
| Reading position lost at power off; second discovery on a key refresh; no on-screen 10% warning | Pass 7: `app_power_off()` saves the position first; discovery runs once per connection; Home shows "charge soon" |
| Phone alerted "update available" for the same version up to hourly; reconnection sync sent twice; stale PROGRESS after QUERY | Pass 8: one alert per version; the app ignores the reconnection data request because `onReady` covers it; largest offset after QUERY |
| Where the source goes | The user chose to push to this repository, `omarkt13/onepage-releases`, branch `claude/magical-edison-9m5j8x` |

# Review loop status

## Final summary (after pass 12)

The review loop ended at the 12-pass limit. The stop rule for two clean
rotations could not be met in 12 passes: passes 10 and 12 were clean, and
pass 11 found two medium issues, which it fixed. No pass found a high issue.
Across all passes: 0 high, 24 medium, 48 low findings; 59 fixed. The rest are open items below.

### State

| Part | State |
|---|---|
| Protocol (`docs/protocol.md`, `shared/`) | Complete. The reference encoder regenerates `vectors.json` with no difference. Firmware and app tests both read the vectors |
| Firmware (`firmware/`) | Complete for bench testing. Builds with MCUboot for the nRF54L15 DK and the custom `onepage_mini` board, no warnings from `firmware/src`. App image 395 KB flash, 223 KB RAM (85 %). Host tests: 3 of 3 suites pass. Never run on hardware |
| App (`app/`) | Complete in code, never compiled. Passes 4 and 10 read every Swift file for compile risks. `OnePageKit` tests (vectors, logic, coverage) have not run |
| Bench plan (`docs/TESTING.md`) | 8 first-build steps for the app, 62 bench tests, measurement table from firmware spec section 11 |

### Verified here / not verified

| Verified | Not verified |
|---|---|
| Protocol byte layout on both sides, against shared vectors (firmware side run; Swift side written) | Any Swift compile or test run |
| Firmware portable logic: privacy timing, calendar, transfer resume, filter, gestures, navigation, reader paging, battery policy | Any run on the DK or the board: Bluetooth with iOS, ANCS, AMS, CTS, NFC, SMP update |
| Firmware compiles and links for both boards with MCUboot | Display, touch, flash, gauge and haptic drivers (datasheet values are placeholders) |
| Signed manifest tool output matches the app's check (checked in Python) | Power, latency and stack figures |

### Open items by group

1. **Needs hardware or datasheets:** item 4 (PCB pins, crystal capacitors),
   item 5 (display commands and waveforms, IQS7222A set-up, S25HL512T sector
   map, MAX17048 4.35 V model, LRA voltages).
2. **Needs a Mac build:** item 6 (first compile, McuManager 1.6 API), item 14
   (run the Swift tests). Optional app changes: items 16, 18.
3. **Needs user decisions:** see "Decisions needed from the user" below:
   module LV or LQ, domain, Bluetooth company ID, firmware and manifest
   signing keys with the manifest URL (also closes item 15), calendar
   default when the phone is away.
4. **Bench tests:** items 1 (T-PWR-3), 2 (T-PWR-4), 3 (T-NOT-8), 9 and 17
   (T-CAL-7, then an app fix), 11 (T-PWR-7), 12 (T-BOOK-6), 13 (T-PWR-6).

### First 5 steps to start bench testing

1. On a Mac, do `docs/TESTING.md` A1–A5: generate the project, run
   `swift test` in `app/OnePageKit`, fix any compile errors, build for a device.
2. Build and flash the DK firmware (`docs/TESTING.md` section 0.2) and open the
   serial log.
3. Run T-PAIR-3 (scan pairing), then T-NOT-1 and T-NOT-2: notifications on the
   DK log with no display fitted.
4. Run T-CAL-1, T-WX-1 and T-BOOK-1 to check the OnePage service end to end.
5. Make the manifest key (A7) and run T-UPD-1 and T-UPD-6 (update and
   rollback) before you fit the panel and the other parts.

Goal: the repository is complete and ready for bench testing.

## Stop rule

The loop stops when two full rotations in a row (12 passes) have no high or medium finding and
all checks pass, or after 12 passes in total. A pass is "clean" when it finds no high or medium
issue and all checks pass.

| Item | Value |
|---|---|
| Last pass | 12 (loop ended at the 12-pass limit) |
| Next area | None. A new loop would start at (a) |
| Consecutive clean passes | 1 |
| Passes left before the 12-pass limit | 0 |

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
| 9 | (c) firmware and app byte by byte | 0 | 2 | 3 | 2 | No | 0db74ab |
| 10 | (d) Swift compile risk | 0 | 0 | 3 | 3 | Yes | 00cf469 |
| 11 | (e) firmware robustness | 0 | 2 | 4 | 6 | No | b9baeaa |
| 12 | (f) test coverage and `docs/TESTING.md` | 0 | 0 | 3 | 3 | Yes | b3e731d |

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
| 16 | 9 | Calendar resume never takes effect: each sync has a new version and `generated_at` (pass-9 L1) | Optional app change; small cost (≤ 16 KB) |
| 17 | 9 | Late calendar DATA failures can use all 3 restarts (pass-9 L2) | Fix together with item 9 |
| 18 | 9 | The CANCEL reply can be read as the DELETE reply (pass-9 L3) | Optional app change; no wrong state |
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
| UIKit import implicit; wrong "not a OnePage Mini" message on a discovery error; alert on every automatic reconnection failure | Pass 10: explicit `import UIKit`; discovery errors show their cause; the paired device retries without an alert |
| Routine PROGRESS made the app resend every block in flight; background reconnection had no background time | Pass 9: `TransferPlan.confirm()` for status OK; `deviceBecameReady()` holds a `BackgroundActivity` |
| One low gauge reading switched the device off; a partial book survived Erase and reset; SUSPEND could turn off a new connection's FAST; off while charging restarted at once; `op/away` survived an erase | Pass 11: `core/battery_policy.c` (voltage check and 60 s confirm, host test); `unlink_dir_all()`; SUSPEND leaves the link state; no STAT1 wake when already charging; `op/away` erased. Bench tests T-PWR-5 (updated), T-PWR-9, T-AWAY-8 |
| Where the source goes | The user chose to push to this repository, `omarkt13/onepage-releases`, branch `claude/magical-edison-9m5j8x` |

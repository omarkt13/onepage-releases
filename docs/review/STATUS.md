# Review loop status

Goal: the repository is complete and ready for bench testing.

## Stop rule

The loop stops when two full rotations in a row (12 passes) have no high or medium finding and
all checks pass, or after 12 passes in total. A pass is "clean" when it finds no high or medium
issue and all checks pass.

| Item | Value |
|---|---|
| Last pass | 1 |
| Next area | (b) app against the app spec and protocol.md |
| Consecutive clean passes | 0 |
| Passes left before the 12-pass limit | 11 |

Rotation: (a) firmware against specs, (b) app against specs, (c) firmware and app byte by byte,
(d) Swift compile risk, (e) firmware robustness, (f) test coverage and `docs/TESTING.md`.

## Pass log

| Pass | Area | High | Medium | Low | Fixed | Clean | Commit |
|---|---|---|---|---|---|---|---|
| 1 | (a) firmware against specs | 0 | 2 | 4 | 3 | No | see git log |

## Open items

| # | From pass | Item | Needs |
|---|---|---|---|
| 1 | 1 | LED blinks from a kernel timer, not a hardware timer | Power measurement at firmware step 4 |
| 2 | 1 | Display supply stays on for 2 minutes after a refresh | Power measurement at firmware step 4 |
| 3 | 1 | A full block queue holds the Bluetooth RX thread for up to 200 ms during a book transfer | Bench test: notification delay during a transfer |
| 4 | before loop | All pins and crystal capacitors on the custom board (`TODO(pcb)`) | PCB design |
| 5 | before loop | Display commands and waveforms, IQS7222A set-up, S25HL512T sector map, MAX17048 4.35 V model, LRA voltages (`TODO(datasheet)`) | Datasheets |
| 6 | before loop | The app was never compiled | A Mac with Xcode |

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
| Where the source goes | The user chose to push to this repository, `omarkt13/onepage-releases`, branch `claude/magical-edison-9m5j8x` |

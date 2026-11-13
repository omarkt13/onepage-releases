# Review pass 12: test coverage and `docs/TESTING.md` (rotation 2, last pass)

Scope: logic added in passes 7–11, bench coverage of every open item and every
fix since pass 6, and the app first-build checklist (A1–A8).

## Findings

| # | Severity | File | Problem | Fix |
|---|---|---|---|---|
| L1 | Low | `docs/TESTING.md` T-PAIR-9 | No pass criterion for the pass 7–10 reconnection fixes: one discovery per connection, one calendar sync and one weather update per reconnection, background time for the reconnection sync, no alert on a failed automatic reconnection | Added these checks, and a repeat of the test with the app in the background and the phone locked |
| L2 | Low | `docs/TESTING.md` T-BOOK-1 | Nothing checked that a routine PROGRESS no longer makes the app send blocks again (pass 9 M1) | Added the check: the app log shows a rewind only after a bad-block reply or QUERY |
| L3 | Low | `docs/TESTING.md` T-UPD-1 | Nothing checked the "update available" alert once per version (pass 8 M1) | Added the check |

## Coverage check, no change needed

| Logic from passes 7–11 | Test |
|---|---|
| `core/battery_policy.c` (pass 11) | Host `battery_policy`: single bad reading, confirm time, voltage floor, unknown voltage, charger, warning hysteresis. Bench T-PWR-5, T-PWR-9 |
| `cal_current_nth()` (pass 6) | Host `cal_running_all_day`; bench T-CAL-6 |
| `xfer_forget()` (pass 5) | Host `transfer_forget`; bench T-BOOK-5 |
| `TransferPlan.confirm()` (pass 9) | Swift `testRoutineProgressConfirmsWithoutRewind` |
| `app_power_off()` position save (pass 7) | Bench T-PWR-2, T-PWR-5 (Zephyr glue, not portable) |
| Erase removes partial books and `op/away` (pass 11) | Bench T-AWAY-7, T-AWAY-8 |
| Largest QUERY offset, update alert per version (pass 8) | App target code (needs UIKit); bench T-BOOK-2, T-UPD-1 |

The remaining untested code is Zephyr or UIKit glue, which only a bench run or a
Mac build can exercise. Every open item in `STATUS.md` that needs a bench run
has a named test.

The app first-build checklist A1–A8 is complete: project generation, `swift
test`, signing, device build, test scheme, WeatherKit capability, manifest key,
install on three phones.

## Checks

| Check | Result |
|---|---|
| Host tests | 3 of 3 suites pass |
| `nrf54l15dk/nrf54l15/cpuapp` with MCUboot | Pass, no warnings from `firmware/src`. App 395 KB flash (55 %), 223 KB RAM (85.2 %) |
| `onepage_mini/nrf54l15/cpuapp` with MCUboot | Pass, no warnings from `firmware/src`. Same size. MCUboot 46 KB of 54 KB |
| `vectors.json` regenerates | No difference |
| Swift | Not compiled: no Swift compiler in this session |

Result: 0 high, 0 medium, 3 low. All 3 fixed. Clean pass.

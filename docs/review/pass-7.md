# Review pass 7: firmware against the specs (rotation 2, area a)

Scope: all of `firmware/`, against `onepage-mini-firmware-spec.md` (draft 2),
`docs/protocol.md` and `docs/TESTING.md`. I also read every firmware change
made in passes 1 to 6 (`git diff 08505af -- firmware/`) to look for
regressions. I found no regressions. Items that `STATUS.md` already lists as
open are not reported again.

## Findings

| # | Severity | File | Problem | Fix | State |
|---|---|---|---|---|---|
| M1 | Medium | `src/app.c` (power menu and 3% battery rule) | Power off went straight to `power_off()`, which closes the book file but does not save the reading position. The device saves the position only every 20 pages, so a power off from the power menu, or the automatic power off at 3%, lost up to 19 pages. The spec asks for a clean switch-off at 3% and says that the reading position lives on the device | New `app_power_off()` calls `reader_close()`, which saves the position, before `power_off()`. Both callers use it | Fixed |
| L1 | Low | `src/ble/ble_core.c` `security_changed()` | Each security change restarted the discovery of the iOS services. A second change in the same connection (a key refresh) would subscribe ANCS again while the first discovery runs, and send a second `EVT_LINK_READY` with a second set of data requests | New `disc_started` flag: discovery runs once per connection. The flag resets on disconnect | Fixed |
| L2 | Low | `src/ui/screens.c` `draw_home()` | Spec section 9 says "Warn at 10%". The device gave only one short vibration, which the user does not notice while the alert strength is "off" | The Home status line shows "- charge soon" after the percent when the battery is at 10% or less and not charging | Fixed |
| L3 | Low | `docs/TESTING.md` T-PWR-2, T-PWR-5 | No test checked the reading position after a power off, the on-screen low-battery warning, or that the watchdog (pass 5) does not wake the device from System OFF | Added these checks to the pass criteria of T-PWR-2 and T-PWR-5 | Fixed |

## Checked with no finding

| Area | Result |
|---|---|
| Phone-away rules (spec section 7) | 2-minute lock, RAM-only notification text, music erased, no alert while locked, 7-day erase with restart carry-over (pass 1), locked Home menu keeps Calendar (by setting), Library and Settings |
| Pairing rules (spec 7, protocol 3 and 4) | Pairing accepted only in the window. The window opens from the reset menu or from NFC without a bond. Opening it erases personal data and the old bond first. An NFC tap with a bond only wakes the screen |
| Security | Every OnePage characteristic and CCC write needs an encrypted link. An unencrypted link is dropped after `ONEPAGE_UNSECURED_TIMEOUT_S` |
| Data requests (protocol 5.6, 8) | Hourly weather, calendar at the 00/06/12/18 slot change, both at reconnection, a retry when weather has expired |
| Alerts | Quiet hours that cross midnight, silent and hide modes, iOS silent and pre-existing flags, per-app pattern, calls use the long pattern |
| Title only | List shows the app and "New notification". Detail shows "Content hidden" |
| Calendar | Atomic swap at COMMIT. Only failures to DATA are answered. Erase clears a partial sync (pass 6) |
| Changes in passes 1 to 6 | Watchdog, SMP fast-link hook, block drain before QUERY and FINISH, `xfer_forget`, newest START wins, `cal_current_nth`, saved time away: no regression found |

## Checks

| Check | Result |
|---|---|
| Host tests | 3 of 3 suites pass |
| `nrf54l15dk/nrf54l15/cpuapp` with MCUboot | Pass. App 394 KB flash (55.2%), 223 KB RAM (85.2%). No warnings from `firmware/src` |
| `onepage_mini/nrf54l15/cpuapp` with MCUboot | Pass. App 395 KB flash (55.3%), 223 KB RAM (85.2%). No warnings from `firmware/src` |
| `vectors.json` regenerates | No difference |

Result: 0 high, 1 medium, 3 low. Not clean (one medium finding).

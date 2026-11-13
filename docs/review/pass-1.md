# Review pass 1: firmware against the firmware spec and protocol.md

Date: 13 Nov 2026. Area (a) of the rotation.

Checked: phone-away rules (spec 7), pairing window and single bond (spec 6.1, 6.5, protocol 3 and 4),
connection parameters (protocol 8), data request schedule (protocol 5.6 and 8), every handler of
the OnePage service (protocol 5), notification limits and filter (spec 5, protocol 5.3), weather
expiry, battery thresholds (spec 9), ghosting rule (spec 9), power button timing (spec 5).

## Findings, by severity

| # | Severity | Where | Problem | Fix | State |
|---|---|---|---|---|---|
| 1 | Medium | `src/core/privacy.c`, `src/app.c` `periodic()` | The 7-day "phone away" timer counts uptime only. A restart while away (flat battery, forced restart) starts it again from zero, so a lost device can keep the calendar for longer than 7 days | Keep a "time away" count. The device saves it to internal storage once an hour while away (`op/away`, minutes), loads it at start-up (`privacy_set_away_before()`), and sets it to 0 when the phone returns. New host test `privacy_restart` | Fixed |
| 2 | Medium | `src/app.c` `make_ctx()` | After a restart without the phone the clock is not valid (`clock_now()` = 0). The Calendar screen then counts every stored event as current, so it shows past events as if they are still to come | The Calendar screen is available only when the clock is valid. The Home "next event" line already had this rule | Fixed |
| 3 | Low | `src/app.c` `battery_check()` | Status was notified every 5 minutes at each battery poll. Protocol 5.1 says on a change of percent or charging state | Notify only when the percent or the charging state changes. Connection and error notifications stay | Fixed |
| 4 | Low | `src/drivers/alerts_hw.c` | The LED blinks from a kernel timer. Spec 8 asks for a hardware timer, so the processor can sleep | Measure LED energy at firmware step 4. If it is significant, move the blink to an nRF PWM sequence | Open |
| 5 | Low | `src/drivers/epd_uc81xx.c` | The display supply stays on for 2 minutes after a refresh. Spec 8 says to cut it between refreshes. A partial refresh needs the last image in the controller RAM | Measure the idle current of the panel at step 4 and decide | Open |
| 6 | Low | `src/ble/op_service.c` `write_xfer_data()` | A full block queue holds the Bluetooth RX thread for up to 200 ms. This is the flow control, but it also delays ANCS traffic during a book transfer | Keep. Check notification delay during a transfer on the bench | Open (bench test) |

## Checked with no finding

- Pairing is refused outside the pairing window. Opening the window erases the owner's data first. An NFC tap opens the window only when no phone is bonded.
- Advertising: 100 ms in the pairing window, 1 s for 30 s after a disconnect, then 2 s, and no advertising with no bond and no window.
- Idle connection request 1815–1965 ms, latency 0, timeout 6 s, sent 60 s after the link is ready. FAST: 15–30 ms with a 30 s return to idle.
- Lock after 2 minutes: notifications and music erased, alerts stopped, full refresh, Notifications and Music not reachable, Calendar per the setting.
- Weather is hidden after "valid until". Weather is requested hourly, and again 10 minutes after it expires.
- Calendar is requested at 00:00, 06:00, 12:00, 18:00 and at reconnection.
- Battery: warning at 10 %, "charge me" and switch off at 3 %.
- Full refresh after 10 partial refreshes and on leaving the reader.
- All OnePage characteristics need an encrypted link. Pairing is possible only in the window, so an encrypted link is a bonded link.

## Verification

| Check | Result |
|---|---|
| Host tests | 2 of 2 suites pass. Behaviour suite: 9 cases, 232 checks |
| Build `nrf54l15dk/nrf54l15/cpuapp` (sysbuild, MCUboot) | Pass, no warnings from `firmware/src`. App 392 KB flash, 222 KB RAM (84.6 %) |
| Build `onepage_mini/nrf54l15/cpuapp` | Pass, no warnings from `firmware/src`. Same memory use |
| `opproto.py` regenerates `vectors.json` | No difference |

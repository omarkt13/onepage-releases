# OnePage Mini: bench test plan

This plan takes the firmware and the app from "builds and passes host tests" to
"works on hardware". It covers the firmware spec (section 11, step 4) and the app
spec (section 9), and every open bench item in `docs/review/STATUS.md`.

Record each run in a copy of the result table at the end. A test passes only when
every pass criterion is met. If a test fails, write the log excerpt and the
firmware and app versions in the table.

## 0. Before you start

### 0.1 Equipment

| Item | Use |
|---|---|
| nRF54L15 DK, then the first OnePage Mini boards | Device under test |
| Sunshine SDE0368NQW-52 panel, IQS7222A strip, S25HL512T flash, DRV2605L with LRA, MAX17048, MCP73833 | Parts that the DK does not have. The firmware runs without each of them and logs that it is missing |
| Nordic Power Profiler Kit II (PPK2) | Current and energy (section 8) |
| Two iPhones: one with the latest iOS, one with the iOS before it; a third model for the test-coverage rule in app spec section 9 | Phone under test |
| A Mac with Xcode 15.4 or later | App build, TestFlight |
| A USB serial terminal at 115200 baud on the DK's VCOM port | Firmware log |
| A stopwatch or a phone camera at 60 fps | Time measurements |

### 0.2 Builds

Firmware (from the repository root, nRF Connect SDK v3.0.2):

```bash
west build -b nrf54l15dk/nrf54l15/cpuapp firmware -d build-dk
```

```bash
west flash -d build-dk
```

Bench build with a 10-minute phone-away erase in place of 7 days (tests T-AWAY-3
and T-AWAY-4 only, never ship it). CMake prints `BENCH BUILD` when it is set:

```bash
west build -b nrf54l15dk/nrf54l15/cpuapp firmware -d build-bench -- -Dfirmware_CONFIG_ONEPAGE_TEST_AWAY_ERASE_MIN=10
```

Stack measurement build (test T-PWR-6):

```bash
west build -b nrf54l15dk/nrf54l15/cpuapp firmware -d build-stack -- -Dfirmware_CONFIG_THREAD_ANALYZER=y -Dfirmware_CONFIG_THREAD_ANALYZER_AUTO=y -Dfirmware_CONFIG_THREAD_ANALYZER_USE_LOG=y -Dfirmware_CONFIG_THREAD_NAME=y
```

Host tests and vectors (must pass before any bench run):

```bash
cmake -S firmware/tests/host -B build-host -G Ninja && cmake --build build-host && ctest --test-dir build-host
```

```bash
python3 -I shared/tools/opproto.py shared/test-vectors/vectors.json && git diff --exit-code shared/test-vectors
```

### 0.3 App: first build on a Mac

The app was written without a Swift compiler. Do these steps in this order and
record each result before the bench tests.

| # | Step | Pass criterion |
|---|---|---|
| A1 | `brew install xcodegen`, then `cd app && xcodegen generate` | `OnePageMini.xcodeproj` is made with no error |
| A2 | `cd app/OnePageKit && swift test` | All tests pass, including `VectorTests`, `LogicTests` and `CoverageTests` |
| A3 | Open the project, select your team, and set the bundle ID | Signing has no error |
| A4 | Build the app for a device (Product > Build) | No compile error. If McuManager calls in `FirmwareUpdater.swift` fail, compare them with iOSMcuManagerLibrary 1.6.x and record the change |
| A5 | Run the `OnePageMiniTests` scheme | All tests pass |
| A6 | Enable the WeatherKit capability for the App ID in the Apple Developer portal, then run the app | Weather loads in the Calendar and Home screen |
| A7 | Make a manifest key pair: `python3 -I shared/tools/make_manifest.py keygen --out ~/onepage-manifest.key`. Put the printed public key in `FirmwareUpdater.manifestPublicKeyBase64`. Keep the private key out of the repository | The Update screen no longer says that updates are not set up |
| A8 | Install on all three test iPhones | The app starts on each |

### 0.4 Log lines to watch

| Log line | Meaning |
|---|---|
| `phone away: locking` | 2-minute rule ran |
| `phone away 7 days: erasing calendar and weather` | Erase rule ran |
| `phone back` | Link returned |
| `NFC tap with no phone: opening pairing` | NFC opened the pairing window |
| `personal data erased` | Erase and reset ran |
| `image confirmed (0)` | A new firmware image was kept |
| `new image not confirmed` | A new image could not start Bluetooth and will roll back |
| `event queue full` | An event was lost. Any occurrence fails the test in progress |

## 1. Pairing

| ID | Setup | Steps | Pass criteria |
|---|---|---|---|
| T-PAIR-1 NFC, app installed | Device erased, app installed, iPhone not paired | 1. Tap the iPhone on the device. 2. Accept both iOS prompts | The app opens on pairing for this device ID. Log shows the NFC line. The device shows "Pairing open for 2 minutes". Main screen in the app shows battery and firmware version. From opening the box to a first notification on the device takes less than 2 minutes (app spec section 9) |
| T-PAIR-2 NFC, app not installed | As T-PAIR-1, app deleted | 1. Tap. 2. Install from the link. 3. Open the app within the 2-minute window | The link opens the App Store page (or the domain's redirect). After install the app finds the device in pairing mode without the tap's device ID |
| T-PAIR-3 Scan | Device erased | 1. Device menu: Settings > Pair a new phone. 2. In the app, scan | The app lists the device. Pairing completes as in T-PAIR-1 |
| T-PAIR-4 Refuse pairing | As T-PAIR-3 | Tap Cancel on the iOS pairing prompt | The app does not show "Connected" and does not keep the device. The message says what happened and what to do next. The device keeps no bond |
| T-PAIR-5 Refuse notification sharing | As T-PAIR-3 | Accept pairing, refuse "share system notifications" | The device shows no notifications. The app explains how to turn sharing on in iOS Settings |
| T-PAIR-6 Tap on a paired device | Device paired to phone A | Tap phone B, which has the app | Phone B opens the app only. The device does not open pairing. Phone A stays paired |
| T-PAIR-7 Window closes | Device erased | Open pairing, wait 2 minutes 10 s | The device stops pairing advertising. The footer says "Pairing closed" |
| T-PAIR-8 Pair a new phone | Device paired, with calendar, weather, filter entries and settings | Pair phone B through Settings > Pair a new phone | Calendar, weather, settings and app filter are erased before the new bond (firmware spec section 7) |
| T-PAIR-9 Reconnection | Paired | 1. Walk out of range for 30 s. 2. Return. 3. Restart the iPhone | The link returns with no user action both times (app state restoration). No "Could not connect" alert shows on the phone while out of range (pass 10). After each reconnection: one calendar sync and one weather update only, each notification shows once on the device (pass 7 L1, pass 8 L1). Repeat step 1–2 with the app in the background and the phone locked: the sync still completes (pass 9 M2). Record the reconnect time in M-6 |

## 2. Notifications

| ID | Setup | Steps | Pass criteria |
|---|---|---|---|
| T-NOT-1 Latency | Paired, connected idle at the long interval for 2 minutes | Send 20 messages from another phone, one every 30 s | Each shows on the device within 2 s of the banner on the iPhone (firmware spec section 9). Record the median and maximum |
| T-NOT-2 Show, Silent, Hide | Three apps in the filter, one in each mode | Send one notification from each | Show: listed, LED and vibration. Silent: listed, no LED or vibration. Hide: not listed, not alerted |
| T-NOT-3 Title only | Set a banking app to title only | Send a notification with text | The device shows the app name and time, not the text, in the list and in the detail |
| T-NOT-4 Quiet hours | Quiet hours on, the current time inside them | Send a notification | Listed, no LED or vibration. Outside quiet hours the same app alerts |
| T-NOT-5 Limits | | Send 55 notifications, one with 600 characters of text | The device keeps the newest 50. The long one is cut at 512 characters at a character boundary (no broken glyph) |
| T-NOT-6 Dismiss and call | | 1. Open a notification and tap. 2. Call the iPhone and tap on the device | 1. The notification goes away on the iPhone too. 2. The call is declined |
| T-NOT-7 New app | An app that never sent a notification | Send one | It appears in the app's filter list as Show, with a name from the built-in table or its raw ID |
| T-NOT-8 During book transfer (STATUS item 3) | Start sending a large book (T-BOOK-1) | Send 10 notifications during the transfer | Each still arrives within 2 s. No `event queue full` |

## 3. Phone away

| ID | Setup | Steps | Pass criteria |
|---|---|---|---|
| T-AWAY-1 Short dropout | Connected, 5 notifications listed | Turn off iPhone Bluetooth for 90 s, then on | Nothing changes on the device. No lock |
| T-AWAY-2 Lock at 2 minutes | As T-AWAY-1 | Turn off Bluetooth for 3 minutes | At 2 minutes (±5 s) the device redraws to the locked Home with "phone not nearby". Notifications and music details are gone. No LED or vibration. When Bluetooth returns, iOS sends the notifications again and the device unlocks |
| T-AWAY-3 Erase (bench build, 10 min) | `build-bench`, calendar and weather synced | Turn off Bluetooth for 12 minutes | At 10 minutes the log shows the erase line. Calendar and weather are gone. Books still open |
| T-AWAY-4 Restart while away (bench build) | As T-AWAY-3 | 1. Turn off Bluetooth. 2. After 6 minutes press reset. 3. Wait | The erase happens at about 10 minutes of total time away, not 10 minutes after the restart (±1 minute, the save step). After the restart the Calendar screen is hidden until the clock is set from the iPhone |
| T-AWAY-5 Calendar hidden setting | Privacy: "Show calendar when phone is away" off | Lock as in T-AWAY-2 | The Calendar item is not available on the locked Home. With the setting on, it is available |
| T-AWAY-6 Flat battery and restart | Notifications listed | Remove power, restore it, keep the iPhone off | No notification text shows after power returns (RAM only) |
| T-AWAY-7 Erase and reset | Paired, with data and books | Device Settings > Erase and reset. Repeat from the app's Privacy screen | Log shows `personal data erased`. Bond, settings, filter, calendar, weather and books are gone. The app shows the device as not paired |
| T-AWAY-8 Erase with a partial book | Stop a book transfer at 50% (T-BOOK-2 first step) | Erase and reset from the device | After the restart, a flash dump or a littlefs listing of `/lfs/b` shows no files: no `.prt` or `.pin` part stays |

## 4. Calendar and weather

| ID | Setup | Steps | Pass criteria |
|---|---|---|---|
| T-CAL-1 First sync | Calendar access given | Pair or tap "Sync now" | The device shows the next 7 days grouped by day, in start order, with "Synced" time |
| T-CAL-2 Background wake | App in the background (not force-quit), phone locked | Wait for the 6-hour slot (or set the iPhone clock so a slot passes) | The device sends a calendar request; the app answers in the background; "Synced" time updates. Record how long the sync takes |
| T-CAL-3 Change on the phone | App open | Add an event on the iPhone | It appears on the device within 1 minute |
| T-CAL-4 Force-quit | Force-quit the app | Wait for the next slot | No update. The device keeps the last calendar. Notifications and music still work. The Help screen says this |
| T-CAL-5 Oversize week | A test calendar with 120 events, many with 80-character titles and 60-character places | Sync | The device shows 100 events or fewer, the earliest ones. No sync error |
| T-CAL-6 All-day plus ended events | Today: one all-day event, two events that ended this morning, one later today | Open Calendar | The ended events do not show. The later event can be selected and opened |
| T-CAL-7 Interrupted sync | Sync a large week | Turn off Bluetooth in the middle | The device keeps its old calendar until a later complete sync. The next sync completes (STATUS item 9: record any failed sync and its log) |
| T-CAL-8 Truncation | Event with a 100-character title with emoji | Sync | Title shows 79 characters and "…". No broken glyph |
| T-WX-1 Weather | Location allowed | Open the app | The device shows temperature, high, low and condition. Apple Weather mark and legal link show in the app |
| T-WX-2 Typed city | Location refused | Type a city | Weather for that city arrives |
| T-WX-3 Expiry | Weather synced | Force-quit the app; wait 2 hours 40 minutes | The device hides the weather after "valid until" (150 minutes after the send) |

## 5. Books

| ID | Setup | Steps | Pass criteria |
|---|---|---|---|
| T-BOOK-1 Send | A 400 KB EPUB without DRM | Add from Files, send | Book appears in the device Library. The app log shows no rewind after a routine PROGRESS, only after a bad-block reply or QUERY (pass 9 M1). Record the speed (spec: 30 to 80 KB/s) |
| T-BOOK-2 Resume | As T-BOOK-1 | Turn off Bluetooth at about 50%; turn it on | The transfer continues from the device's offset, not from 0. The finished book opens and its CRC matches |
| T-BOOK-3 Background | As T-BOOK-1 | Lock the iPhone during the transfer | The transfer continues while iOS allows, then resumes when the app returns |
| T-BOOK-4 Restart a partial book | Stop a transfer at 50% | Send another book | The new book transfers. The device discards the old partial book (no "busy") |
| T-BOOK-5 Delete during transfer | As T-BOOK-1 | Delete the book in the app during the transfer | The transfer stops. No partial file stays. The next book transfers |
| T-BOOK-6 Delete the open book (STATUS item 12) | A book open in the device reader | Delete it in the app, then turn a page on the device | The reader closes and the Library shows. At most one page turn shows old text. No crash |
| T-BOOK-7 Reader | A long book | Turn 50 pages forward, 50 back, close, open | Each page turn under 0.7 s. A full refresh after each 10 partial refreshes. Position is kept after close and after a restart |
| T-BOOK-8 Storage full | Fill the flash | Send one more book | The app shows "no space" with what to do next |
| T-BOOK-9 Copy-protected | An EPUB with DRM | Import | The app refuses it with a clear message |

## 6. Firmware update

Make the manifest with `shared/tools/make_manifest.py sign` over
`build-*/firmware/zephyr/zephyr.signed.bin`, and check it with
`make_manifest.py verify` before the test.

| ID | Setup | Steps | Pass criteria |
|---|---|---|---|
| T-UPD-1 Success | Version N on the device, N+1 in the manifest, battery over 30% | Update from the app | Progress shows; the device restarts; log shows `image confirmed (0)`; Status reports N+1. Before the update, the phone shows the "update available" alert once for N+1, and not again after the next daily check (pass 8 M1). Record the duration |
| T-UPD-2 Battery gate | Battery under 30%, no charger | Try the update | The app refuses it and says to charge. On the charger it is allowed |
| T-UPD-3 Bad signature | Change one byte of `size` in the manifest | Check for updates | The app refuses it as a bad signature |
| T-UPD-4 Bad hash | Serve a different image with the old manifest | Update | The app refuses it after the download (hash) |
| T-UPD-5 Link lost | During upload, turn off Bluetooth | Turn it on and update again | The device stays on version N and works. The second update succeeds |
| T-UPD-6 Rollback | Build N+1 with `ble_init()` made to return an error on purpose, and sign it | Update | The log shows `new image not confirmed`; the device restarts into version N and the app can connect again |
| T-UPD-7 MCUboot key | Sign an image with a different MCUboot key | Update | MCUboot refuses the image; the device stays on version N. Before production, replace the development key (STATUS decision 4) |

## 7. Power, timing and stacks

| ID | Setup | Steps | Pass criteria |
|---|---|---|---|
| T-PWR-1 Idle current | PPK2 on the battery input, connected idle, screen still | Record 10 minutes at each interval (M-1) | Target 30 µA average at the long interval (firmware spec section 8) |
| T-PWR-2 Shipping and away | Power menu > Power off; then a bonded device with the phone away | Record 5 minutes each | Off under 2 µA. The device stays off for the full 5 minutes: the watchdog does not wake it from System OFF. Away (slow advertising) under 20 µA |
| T-PWR-3 LED timer (STATUS item 1) | Alert pattern on the LED with the CPU otherwise idle | Record the current during a blink pattern | Record the extra charge per pattern. If it is more than 10% of the idle budget per hour of normal use, move the LED to a hardware timer |
| T-PWR-4 Display supply hold (STATUS item 2) | Refresh once, then idle | Record 3 minutes | Record the current during the 2-minute hold and after the supply is cut. Decide the hold time from M-4 and M-5 |
| T-PWR-5 Battery levels | Adjustable supply in place of the cell | Lower the voltage | Warning at 10%: one vibration and "charge soon" on Home. At 3% and 3.55 V or less, the "charge me" screen shows within about 5 minutes (two low readings at least 60 s apart) and the device turns off cleanly. At 3.30 V or less it turns off whatever the percentage. A single low reading (for example a brief dip during a full refresh) does not turn it off. Repeat with a book open: after the restart the book opens at the same page. Also check Power menu > Power off with a book open |
| T-PWR-9 Off while charging | On the charger | Power menu > Power off | The device stays off (it does not restart at once). The power button switches it on. Then, switched off on battery, connecting the charger switches it on |
| T-PWR-6 Stacks (STATUS item 13) | `build-stack` | Run T-NOT-1, T-BOOK-1, T-CAL-1 and T-UPD-1 with the analyzer log on | Each thread keeps at least 25% of its stack unused. Record the values and reduce or increase stack sizes |
| T-PWR-7 Fast-link state (STATUS item 11) | Connected | Run a book transfer, a calendar sync and an update check at the same time, 5 times | The link returns to the long interval within 30 s after the last task. No stuck fast interval (watch the PPK2 current) |
| T-PWR-8 Watchdog | Debug build with a forced hang in the application thread | Trigger the hang | The device restarts after the watchdog time. The bond and settings are kept |

### 7.1 Measurement table (firmware spec section 11, step 4)

| # | Measurement | Condition | Value | Target |
|---|---|---|---|---|
| M-1a | Idle current | Connection interval 15–30 ms | | Reference |
| M-1b | Idle current | Connection interval about 500 ms | | Reference |
| M-1c | Idle current | Connection interval 1815–1965 ms | | 30 µA |
| M-2 | Energy per notification | From arrival to display done, partial refresh | | Record |
| M-3 | Energy per notification with vibration | Medium alert strength | | Record |
| M-4 | Energy per full refresh | | | Record |
| M-5 | Energy per partial refresh | | | Record |
| M-6 | Button to readable screen | From idle | | Under 1 s |
| M-7 | Page turn | Partial refresh | | Under 0.7 s |
| M-8 | Time to reconnect | Phone returns after 5 minutes away | | Record |
| M-9 | Notification latency | Median and maximum of T-NOT-1 | | Under 2 s |
| M-10 | Book transfer speed | T-BOOK-1 | | 30–80 KB/s |
| M-11 | Battery life estimate | From M-1c, M-2 and 50 notifications a day | | Record |

## 8. Accessibility and errors (app)

| ID | Steps | Pass criteria |
|---|---|---|
| T-ACC-1 | Set the largest Dynamic Type size; visit every screen | No text is cut or overlapping |
| T-ACC-2 | Turn on VoiceOver; visit every screen | Every control has a label that says what it does |
| T-ACC-3 | Cause each error once: Bluetooth off, permission refused, transfer failed, update refused | Each message says what happened and what to do next |

## 9. Result table

| Test | Date | Firmware | App | iPhone and iOS | Result | Notes |
|---|---|---|---|---|---|---|
| | | | | | | |

# Review pass 2: app against the app spec and protocol.md

Area (b). Files reviewed: `app/OnePageMini/**` and the app-facing parts of
`app/OnePageKit` (calendar blob, transfer, filter, update). Swift compile risk
is area (d) and is not part of this pass.

Result: 0 high, 3 medium, 4 low. 5 fixed, 2 left open.

## Medium

| # | File | Problem | Fix | State |
|---|---|---|---|---|
| M1 | `OnePageMini/BLE/DeviceManager.swift`, `didUpdateNotificationStateFor` | If the user declines the iOS pairing prompt, the subscribe calls fail, but the app still called `linkReady`. It showed "Connected" and saved the peripheral as paired. Then every read failed, and the app never offered pairing again. Spec 4.1 step 3 and spec 9 "every failure names what happened and what to do next" | Count subscribe failures. If any failed, do not call `linkReady`. New `abandonSetup` cancels the connection for a device that is not yet paired and sets state `idle`. A paired device stays the reconnection target. Service and characteristic discovery errors also call `abandonSetup`, so the state no longer stays at "Connecting" | Fixed |
| M2 | `OnePageMini/Views/CalendarHomeView.swift`, `Services/WeatherProvider.swift` | The app shows WeatherKit data but no Apple Weather mark and no link to the legal attribution page. Apple requires both for any app that uses WeatherKit. App Review and TestFlight external review reject apps without them | `WeatherProvider.loadAttribution()` reads `WeatherService.shared.attribution`. The Weather section footer shows the combined mark (light or dark) and a "Weather data sources" link to `legalPageURL` | Fixed |
| M3 | `OnePageMini/Services/FirmwareUpdater.swift` | Firmware spec section 8: the link uses the shortest interval during an update. The app never sent FAST before the SMP upload, and the firmware does not speed up the link for SMP traffic. At the idle interval of about 2 s, a 400 KB image takes hours | `FirmwareUpdater.device` is set by `AppModel`. During the upload, a task sends FAST on every 10 s, because the device returns to idle 30 s after the last FAST. `cleanUp` stops the task and sends FAST off | Fixed in the app. See open item L4 for the firmware side |

## Low

| # | File | Problem | Fix | State |
|---|---|---|---|---|
| L1 | `OnePageMini/Services/WeatherProvider.swift` | In set-up, "Use approximate location" asks for permission and moves on. No weather was sent until the next hourly request | When authorization changes to allowed while weather and location are on, send weather at once | Fixed |
| L2 | `OnePageMini/Views/CalendarHomeView.swift` | "Last sent" always showed °C with one decimal, also when the user picked °F | Show the value in the chosen unit, rounded to a whole degree, as the device does | Fixed |
| L3 | `OnePageMini/Services/BookLibrary.swift`, `send` | If a transfer of book A breaks and the user then deletes A on the phone only, START for book B can get status 5 "busy". The app marks B as failed, and the user cannot clear the partial book from the app | Either the firmware drops a partial book when START names another book, or the app sends CANCEL for the partial book id it knows. Decide in pass (c), which cross-checks both sides | Open |
| L4 | `firmware/src/ble/ble_core.c` | The firmware does not ask for the fast interval when SMP traffic starts. The app now covers this, but an update started from another tool (nRF Connect Device Manager) runs at the slow interval | Refresh the fast-mode timer on SMP traffic, for example with an MCUmgr command callback. Pass (e) | Open |

## Checked and conforming

- Pairing flow: one page per permission, the two iOS prompts explained before they appear, NFC target auto-connect, "a tap on a paired device only opens the app".
- Notification filter: Show, Silent and Hide, alert pattern, title only, the "title only" suggestion for banking and sign-in apps, raw identifier for unknown apps, quiet hours.
- Calendar: full access asked when the user turns the feature on, 7 days, running events kept, truncation rules, BEGIN/DATA/COMMIT with resume from `next_offset`, CLEAR when sync is turned off, sync on calendar change with a 2 s debounce.
- Books: Files importer and share sheet, conversion on the phone, resume from the device offset, delete, progress from the device position.
- Update: daily check, signature, size and SHA-256 checks, 30% battery or charger rule, clear failure text.
- Privacy screen: setting, title-only count, fixed rules in plain words, erase with the iOS "forget device" step.
- Help screen: force-quit behaviour, 6-hour calendar delay, the iOS limits from spec section 8.
- Info.plist and entitlements: Bluetooth, full calendar access, reduced-accuracy location, `bluetooth-central` background mode, document types, WeatherKit, associated domains.

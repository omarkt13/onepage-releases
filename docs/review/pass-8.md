# Review pass 8: app against specs (rotation 2)

Area (b). Scope: all of `app/OnePageMini`, checked against the app spec, `docs/protocol.md` and
`docs/TESTING.md`, with a regression check of the app changes from passes 2 to 6. The open items
in `STATUS.md` are not reported again.

Result: 0 high, 1 medium, 4 low. All 5 are fixed. No regressions found from passes 2 to 6.

## Findings

| # | Severity | File | Problem | Fix |
|---|---|---|---|---|
| M1 | Medium | `app/OnePageMini/Services/LocalAlerts.swift:12`, `App/AppModel.swift:71` | Between daily checks, `FirmwareUpdater.check` returns the stored manifest, and `checkFirmware` posts "update available" every time. It runs at each reconnection and each data request, so the phone alerted about the same version up to once an hour. The same notification ID replaces and shows the alert again | `LocalAlerts.updateAvailable` posts once per version, recorded in the new `Prefs.lastUpdateAlertVersion` |
| L1 | Low | `app/OnePageMini/App/AppModel.swift:60` | At each reconnection the device sends a data request (reason 1), and `deviceBecameReady` also syncs. When the request arrived after the link was ready, the app sent the week twice and made two WeatherKit calls | `handle(_:)` ignores reason `reconnected`. `onReady` runs on every connection, including restored background launches, so nothing is lost. If the request arrives before the link is ready, the old code already did nothing |
| L2 | Low | `app/OnePageMini/Services/FirmwareUpdater.swift:52` | If `refreshAll` failed, `status.firmware` is nil, and the automatic check used up the daily slot with no result for 24 hours | An automatic check without a device version returns at once and does not record the check time |
| L3 | Low | `app/OnePageMini/Services/BookLibrary.swift:208` | A PROGRESS sent for an earlier batch of 32 blocks can arrive before the QUERY reply. The app then used an old offset and sent blocks again, which costs a round (limit: 20) | After QUERY, the app also drains the mailbox and uses the largest offset for this book. In one session the device's offset only grows. `TransferPlan.apply` does not change, because a new START can still move the offset back |
| L4 | Low | `app/OnePageMini/Views/DeviceSettingsView.swift:18` | The "Screen refresh" picker was enabled while the device was not connected. A change failed with an error, and the other pickers were already disabled | The section is disabled while not connected |

## Checked, no finding

- Device screen: connection, battery, charging, storage, firmware, "update available" (app spec 3).
- Notifications: Show, Silent and Hide, alert pattern, title only, quiet hours, title-only suggestions.
- Library: Files import, share sheet, delete with CANCEL or DELETE, resume after reconnection.
- Calendar and Home: calendar choice, last sync, "sync now", weather place, unit, clock, Apple Weather mark.
- Privacy: phone-away setting, the fixed rules from app spec 4.6, erase with confirmation.
- Device settings, Update and Help screens against app spec 3, 4.5, 5 and 8.
- Fast-link reference count (pass 3), WeatherKit attribution (pass 2), pairing refusal path (pass 2),
  update fast-link hold (pass 2), large-week trimming (pass 3), `@MainActor` on `OnboardingView` (pass 4).

## Checks

| Check | Result |
|---|---|
| Firmware host tests | 3 of 3 suites pass |
| `vectors.json` regenerates | No difference |
| Firmware builds | Not run: no firmware or shared file changed |
| Swift compile | Not possible here (open item 6) |

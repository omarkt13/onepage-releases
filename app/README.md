# OnePage Mini: iPhone app

SwiftUI app for iOS 17 and later. It implements `onepage-mini-app-spec.md`
(draft 2) and speaks the Bluetooth protocol in [`../docs/protocol.md`](../docs/protocol.md).

## Build

1. Install Xcode 15.4 or later and [XcodeGen](https://github.com/yonaskolb/XcodeGen).
2. Generate the project:

```bash
cd app && xcodegen generate
```

3. Open `OnePageMini.xcodeproj`, set your team on the `OnePageMini` target, and run on an iPhone.
   Bluetooth does not work in the Simulator.

## Tests

The protocol, book conversion and transfer logic live in the `OnePageKit` Swift
package. It uses Foundation only, so it tests on macOS or Linux without a phone:

```bash
cd app/OnePageKit && swift test
```

`VectorTests` reads `shared/test-vectors/vectors.json`. The firmware host tests
read the same file, so a protocol change that is made on one side only fails a test.
To change the protocol, change `shared/tools/opproto.py`, regenerate the vectors,
then fix both sides.

The app's own tests (`OnePageMiniTests`) run in Xcode with the `OnePageMini` scheme,
which also runs the package tests.

## Apple capabilities

| Capability | Why |
|---|---|
| Background mode: Uses Bluetooth LE accessories (`bluetooth-central`) | Stay linked with the app closed. The device's data request wakes the app |
| WeatherKit | Weather for the device's Home screen. Enable it for the App ID in the developer portal too |
| Associated Domains: `applinks:onepage.app` | The NFC tag's link opens the app |
| Privacy strings | Bluetooth, full calendar access, location when in use (approximate by default) |
| Document types | EPUB and plain text, so "Open in OnePage" works from the share sheet |

## Placeholders to replace before release

| Placeholder | Where | What it needs |
|---|---|---|
| `onepage.app` domain | `OnePageKit/Sources/OnePageKit/DeviceLink.swift` (`PairingLink.host`), `project.yml` entitlements | The real domain. It must serve `/.well-known/apple-app-site-association` for paths `/p/*`, and send visitors without the app to the App Store. Keep it equal to the firmware's NDEF record |
| Firmware manifest URL | `OnePageMini/Services/FirmwareUpdater.swift` (`manifestURL`) | The update server |
| Manifest signing key | `FirmwareUpdater.manifestPublicKeyBase64` | Base64 of a 32-byte Ed25519 public key. While empty, the app refuses every update with a clear message |
| Bluetooth company ID `0xFFFF` | `OnePageKit/.../DeviceLink.swift` (`AdvertInfo.companyID`) | The assigned Bluetooth SIG company ID. Change the firmware at the same time |
| Team ID and bundle ID | `project.yml` | Your Apple developer account |

### Firmware manifest format

```json
{ "version": "1.0.1", "url": "https://…/onepage-mini-1.0.1.bin", "size": 312344,
  "sha256": "<64 hex>", "signature": "<base64>", "notes": "…", "hw": [1] }
```

`signature` is Ed25519 over `"<version>\n<size>\n<sha256>"`. The app checks the
signature, then the size and SHA-256 of the download, then sends the MCUboot image
with Nordic's McuManager library. MCUboot checks its own image signature as well.

## Module map

| Path | Job |
|---|---|
| `OnePageKit/Sources/OnePageKit/` | Platform-independent logic |
| ├ `Bytes.swift`, `TLV.swift`, `Checksums.swift` | Byte reader and writer, TLV records, CRC-32, CRC-16 |
| ├ `Messages.swift` | UUIDs, Status, Settings, Home data, Data request, Privacy, error codes |
| ├ `AppFilter.swift` | App filter entries and commands, known app names |
| ├ `Calendar.swift` | Calendar blob, truncation rules, sync commands, local seconds |
| ├ `Transfer.swift` | Book transfer commands, replies, blocks, resume plan |
| ├ `CP1252.swift`, `OPB.swift`, `BookConverter.swift` | Book character set, `.opb` file, EPUB and text conversion |
| ├ `Zip.swift`, `Inflate.swift`, `MiniXML.swift` | Minimal EPUB reading: zip, DEFLATE (Compression framework, with a pure-Swift fallback), XML tokens |
| ├ `DeviceLink.swift` | Advertising data, device ID, NFC link parsing |
| └ `Weather.swift`, `FirmwareManifest.swift` | WeatherKit condition mapping, update manifest, version compare |
| `OnePageMini/App/` | App entry, service wiring, preferences, errors, background tasks |
| `OnePageMini/BLE/DeviceManager.swift` | CoreBluetooth central with state restoration, GATT queue, notifications |
| `OnePageMini/Services/` | Calendar (EventKit), weather (WeatherKit, CoreLocation), books, firmware update, phone alerts |
| `OnePageMini/Views/` | Set-up and pairing, Device, Notifications, Library, Calendar and Home, Privacy, Device settings, Update, Help |

## How the app uses the link

| Task | Characteristics | Notes |
|---|---|---|
| First connection | Subscribe to Status, App filter, Calendar sync, Data request, Transfer control | The first encrypted subscribe makes iOS show its pairing prompt |
| Weather | Home data | On a data request, at connection, and when the user changes the place or unit |
| Calendar | Transfer control `FAST` on, Calendar sync BEGIN, DATA…, COMMIT, `FAST` off | DATA uses write with response. `FAST` shortens the connection interval so a week fits in the few seconds iOS gives in the background |
| Books | Transfer control START, Transfer data blocks, QUERY, FINISH | Blocks are writes without response. The app follows the device's resume offset, so a dropped link or a closed app resumes where it stopped |

## Open items

1. **App Clip** (spec 4.1) is not built. A short test must first show whether an App Clip can complete Bluetooth pairing and the notification-sharing prompt.
2. **Not compiled yet.** This code was written without access to Xcode or a Swift compiler. Expect a first round of small compile fixes. The places most likely to need one are the McuManager calls in `FirmwareUpdater.swift` (the library's API changed between versions; it is pinned from 1.6.0), and `WeatherCondition.rawValue` in `WeatherProvider.swift`.
3. **Device ID print order.** The protocol says the device ID is "the low 4 bytes of FICR.DEVICEADDR, shown as 8 hex characters" without the byte order. The app prints the bytes as a little-endian `u32` (`%08X`) and accepts both orders when it matches an NFC link. Fix the order in `docs/protocol.md`.
4. **Daylight-saving change inside the synced week.** Events use local wall-clock seconds, so an event after a clock change shows at its correct local time, but the device's "now" is one hour off until it next reads the iPhone's time.

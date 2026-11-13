# OnePage — releases

Public download point for OnePage app builds.

**Boox Palma / Android e-ink:** grab the latest APK from
[Releases](../../releases/latest) — direct link:
[`OnePage.apk`](../../releases/download/palma-latest/OnePage.apk)

Install guide: open the link on the device, allow installs from your
browser when prompted, tap Install. Updates install over the top.

## OnePage Mini (e-ink device for iPhone)

| Folder | Contents |
|---|---|
| [`firmware/`](firmware/) | nRF54L15 firmware (nRF Connect SDK v3.0.2, MCUboot). See its README for build and flash steps |
| [`app/`](app/) | iPhone app (SwiftUI, iOS 17) and the `OnePageKit` package. See its README for the Xcode set-up |
| [`docs/protocol.md`](docs/protocol.md) | Bluetooth protocol between the device and the app |
| [`docs/TESTING.md`](docs/TESTING.md) | Bench test plan and app first-build steps |
| [`docs/review/STATUS.md`](docs/review/STATUS.md) | Review status, open items and decisions needed |
| [`shared/`](shared/) | Reference protocol encoder, test vectors, update manifest tool |

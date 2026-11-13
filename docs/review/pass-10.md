# Review pass 10: Swift compile risk (area d, rotation 2)

Scope: every Swift file in `app/` (OnePageKit sources and tests, the app
target, the app tests) and `app/project.yml`, read again with no compiler.
Extra attention went to the code added in passes 4 to 9
(`git diff 3b59e96 HEAD -- app/`): `TransferPlan.confirm`, the QUERY offset
merge, `deviceBecameReady` background time, `LocalAlerts.updateAvailable`,
`CoverageTests.swift` and its manifest fixture.

Open items 6 and 14 (no real build, new Swift tests not run) stay open and are
not reported again.

## Result

0 high, 0 medium, 3 low. All 3 are fixed. The pass is clean.

## What was checked and found correct

| Area | Check |
|---|---|
| Actor isolation | Every `@MainActor` function is called from `body`, a `@MainActor` function, or a `Task { @MainActor in … }`. `BackgroundActivity` and `OnboardingView` are `@MainActor`. No `Binding` set closure calls a `@MainActor` function synchronously, so the code also builds with SDKs that make `Binding` closures `@Sendable` |
| Observation | `@Observable` classes use `@ObservationIgnored` only on `var`; `let` properties are not tracked; `didSet` on tracked properties is supported |
| Pattern syntax | `for case let .progress(id, status, o) in … where …`, `if case let .result(_, status)? = try? await …`, `case let .setResult(r), let .removeResult(r)`, `catch LinkError.notConnected` |
| Passes 4–9 code | `TransferPlan.confirm` and `apply` match the tests in `CoverageTests.swift` by hand trace; the QUERY offset merge compiles (`TransferStatus` is `Equatable` by raw value) |
| Manifest fixture | `Fixtures/manifest-test.json` checked in Python: the Ed25519 signature verifies, the size is 240 and the SHA-256 matches, so `testManifestSignatureFromReleaseTool` will pass |
| Linux | OnePageKit uses no Apple-only API outside `#if canImport`. `String(format:)` is used only with integer arguments, which are `CVarArg` on Linux |
| SwiftUI API | `onChange(of:) { old, new in }`, `ContentUnavailableView`, local `@Bindable`, `UTType.epub`, `LabeledContent`, `AsyncImage` with `WeatherAttribution` URLs: all iOS 17 |
| EventKit, WeatherKit, CoreLocation | `requestFullAccessToEvents()`, `.fullAccess`/`.writeOnly`, `EKEvent.startDate` as an implicitly unwrapped optional, `WeatherService.shared.attribution`, `dailyForecast.first`, async `CLGeocoder` calls |
| `project.yml` | Valid YAML. The scheme's `package: OnePageKit/OnePageKitTests` test entry, `TEST_HOST`/`BUNDLE_LOADER`, the excluded plist and entitlements, and the pinned McuManager 1.6.x are in XcodeGen form |

## Findings

| # | Severity | File | Problem | Fix (applied) |
|---|---|---|---|---|
| L1 | Low | `app/OnePageMini/App/AppModel.swift:4`, `app/OnePageMini/Views/Components.swift:2` | Both files use `UIApplication` with only `import SwiftUI`. This builds today because SwiftUI exposes UIKit on iOS, but Apple does not document it | Added `import UIKit` to both files |
| L2 | Low | `app/OnePageMini/BLE/DeviceManager.swift:580` | `didDiscoverServices` reported every error, for example a link drop, as "This is not a OnePage Mini". The user then doubts the right device | An `error` now shows "Could not set up the device" with the real cause. The "not a OnePage Mini" message shows only when the service is missing |
| L3 | Low | `app/OnePageMini/BLE/DeviceManager.swift:553` | `didFailToConnect` showed a "Could not connect" alert also for the automatic reconnection of the paired device, which retries on its own and often runs in the background | The alert now shows only for a device the user picked for pairing. The paired device retries without an alert |

## Checks

| Check | Result |
|---|---|
| Host tests | 3 of 3 suites pass |
| `python3 -I shared/tools/opproto.py` | `vectors.json` regenerates with no difference |
| Firmware builds | Not run: no firmware or shared file changed |
| Swift build and `swift test` | Not run: no Swift compiler in this session (open items 6 and 14) |

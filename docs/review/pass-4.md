# Review pass 4: Swift compile risk

Area (d). There is no Swift compiler in this session, so I read every Swift file in `app/`:
the 15 `OnePageKit` sources, 2 package test files, 21 app sources, the app test file,
`project.yml` and `Package.swift`. For each file I checked the API calls and signatures, the
types and optionals, actor isolation, `@Observable` use, `Codable`, the imports, whether each
`switch` covers every case, and whether `OnePageKit` builds on Linux.

The baseline is Xcode 15.4 or later (`app/README.md`), Swift 5 language mode (`SWIFT_VERSION: "5.0"`),
and the default ("minimal") concurrency checking. With these settings, missing `Sendable`
conformances give warnings. Actor-isolation violations give errors.

Result: 0 high, 2 medium, 6 low. I fixed all 8.

## Medium

| # | File | Problem | Fix | State |
|---|---|---|---|---|
| M1 | `app/OnePageMini/Views/OnboardingView.swift` `bluetooth` (line 76 before the fix) | With the iOS 17 SDK (Xcode 15.4, the lowest supported version), `View` isolates only `body` to the main actor. The page properties (`bluetooth`, `calendarPage` and the others) are not isolated. `Button("Open Settings") { openAppSettings() }` calls a `@MainActor` synchronous function from a nonisolated closure. In Swift 5 mode this is an **error**. Also, the `Task { … next() }` blocks in those pages could set `@State` off the main thread. The iOS 18 SDK makes all of `View` main-actor isolated, so Xcode 16 hides the problem | Mark `OnboardingView` `@MainActor`. The other views call main-actor code only from `body` | Fixed |
| M2 | `app/project.yml` `packages.McuManager` | `from: "1.6.0"` accepts every 1.x release. `FirmwareUpdater.swift` uses `McuMgrBleTransport(_:)`, `FirmwareUpgradeManager(transport:delegate:)`, `McuMgrPackage(from:)`, `start(package:)` and the six `FirmwareUpgradeDelegate` methods, all written against 1.6. A new minor release that changes one signature breaks the first build on a clean machine, and nothing in this repository can find that first | Pin with `minorVersion: "1.6.0"`, which accepts 1.6.x only. Move to a newer minor only after a build and an update test on a device. `app/README.md` updated | Fixed |

## Low

| # | File | Problem | Fix | State |
|---|---|---|---|---|
| L1 | `CalendarService.swift:9`, `WeatherProvider.swift:11-12` | `@ObservationIgnored` on `let` properties. `@Observable` never tracks a `let`, and putting an accessor macro on a `let` can give a diagnostic | Remove the attribute. Behaviour does not change | Fixed |
| L2 | `OnePageKit/Sources/OnePageKit/BookConverter.swift:26-27, 124` | `(fileName as NSString)` and `(opfPath as NSString)`. The `as` bridge from `String` to `NSString` is not always available on Linux, where the README says `swift test` runs | New `PathParts.split` and `PathParts.directory` in plain Swift. New test `testPathParts` | Fixed |
| L3 | `OnePageKit/Sources/OnePageKit/Weather.swift` `fromWeatherKit` | The map compared case-sensitive strings with `WeatherCondition.rawValue`. If the raw values use the REST spelling (`"PartlyCloudy"`), every condition maps to "unknown", and no build error shows it | Compare in lower case. New assertion in `testWeatherMapping` | Fixed |
| L4 | `BookLibrary.swift:96` `Task.detached { … } -> OPBBook` | `Task` needs `Success: Sendable`. Public structs from another module do not get an implicit `Sendable` conformance. In Swift 5 mode this is a warning, and in Swift 6 it is an error | Add `Sendable` to `OPBBook`, `OPBBook.Chapter`, `CalendarEvent`, `DeviceBook`, `AppFilterEntry`, `FilterMode`, `AlertPattern`, `TransferStatus` and `WeatherCode`. All of their stored members are value types | Fixed |
| L5 | `OnePageKit/Sources/OnePageKit/Messages.swift:288` | `try` inside one branch of a `?:` expression. It is legal because the `try` is inside a call argument, but it is the form that Swift rejects when the `try` is a direct operand | Write it as `if`/`else` | Fixed |
| L6 | `OnePageKit/Tests/OnePageKitTests/VectorTests.swift` (`int`, `all_day`, chapter offsets) | Values from `JSONSerialization` were read with `as? NSNumber`, `as! NSNumber` and `as? Bool`. On Linux, swift-corelibs-foundation can return `Int` and `Bool`, so the cast fails or traps, and `all_day` reads as false | `number(_:)` and `flag(_:)` accept `Int`, `Double`, `Bool` and `NSNumber` | Fixed |

## Checked and conforming

- **CoreBluetooth:**
  - The `CBCentralManager(delegate:queue:options:)` options include the restore identifier. `scanForPeripherals`, `retrievePeripherals`, `retrieveConnectedPeripherals` and `maximumWriteValueLength(for:)` are used correctly.
  - The writes use `canSendWriteWithoutResponse` and `peripheralIsReady(toSendWriteWithoutResponse:)`.
  - `CBATTError.Code(rawValue:)` and `CBATTErrorDomain` are used correctly.
  - All 5 central delegate signatures and all 6 peripheral delegate signatures are correct.
- **EventKit (iOS 17):** `requestFullAccessToEvents()`, `.fullAccess` and `.writeOnly`, `predicateForEvents(withStart:end:calendars:)`, the `startDate` and `endDate` implicitly unwrapped optionals, and `EKCalendar.cgColor` with `Color(cgColor:)`.
- **WeatherKit:** `WeatherService.shared.weather(for:)`, `currentWeather.temperature`, `dailyForecast.first` with its high and low temperatures, and the `attribution` properties `combinedMarkLightURL`, `combinedMarkDarkURL`, `legalPageURL` and `serviceName`.
- **CoreLocation:** the `authorizationStatus` instance property, `kCLLocationAccuracyReduced`, `requestLocation()` with both delegate callbacks, and async `geocodeAddressString` and `reverseGeocodeLocation`.
- **UserNotifications:** async `requestAuthorization(options:)`, and `add(_:)` called from a synchronous context, where it uses the completion-handler form.
- **UniformTypeIdentifiers:** `UTType.epub` and `UTType.plainText`. `.fileImporter` returns `Result<[URL], Error>`.
- **Compression:** guarded with `#if canImport(Compression)`. The arguments to `compression_decode_buffer` and the `COMPRESSION_ZLIB` constant (raw DEFLATE) are correct.
- **CryptoKit:** `Curve25519.Signing.PublicKey(rawRepresentation:)`, `isValidSignature(_:for:)` and `SHA256.hash(data:)`.
- **Isolation:**
  - Every `@MainActor` call site (`DeviceManager` GATT methods, `Mailbox.next`, `BackgroundActivity`, `openAppSettings`, `BookLibrary.retry`, `FirmwareUpdater.cancel`) is called from main-actor code, from a `Task { @MainActor … }`, or with `await`. The `OnboardingView` call sites were the only exception, and M1 fixes them.
  - The delegate callbacks are nonisolated, and they reach main-actor code only through `Task { @MainActor … }`.
- **`@Observable`:**
  - Tracked `var` properties with `didSet`, on `NSObject` subclasses with an `override init`.
  - Assignments in `init` before `super.init` touch ignored properties only.
  - `@Bindable` locals are used in `body`, and `.environment(model)` with `@Environment(AppModel.self)`.
- **Switches:** they cover every case of `LinkState`, `UpdateView` phases (all 9), `OnboardingView.Step` (all 8), `TransferStatus`, `DeviceErrorCode`, `ChargingState`, `XMLToken` and `CalendarCommand`. `CBManagerState` and `FirmwareUpgradeState` use `default`.
- **`project.yml`:** valid XcodeGen keys (`info`, `entitlements`, `package` and `product` dependencies, `bundle.unit-test` with `TEST_HOST`, and a package test target in the scheme). `Package.swift` uses tools 5.9 and excludes `Fixtures`.
- **Linux build of `OnePageKit`:** after L2 it uses Foundation only, with no Objective-C bridging. The `String(format:)` calls use integer arguments only.

## Not checked here

These need Xcode and are already open item 6 in `STATUS.md`:
- whether the McuManager 1.6 signatures match the code exactly
- whether the iOS 17 SDK accepts every SwiftUI modifier
- the first `swift test` run

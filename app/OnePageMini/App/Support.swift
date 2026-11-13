import Foundation
import OnePageKit

/// An error shown to the user: what happened, and what to do next.
struct UserFacingError: Identifiable, Equatable {
    let id = UUID()
    var title: String
    var message: String

    static func == (a: UserFacingError, b: UserFacingError) -> Bool { a.id == b.id }

    init(title: String, message: String) {
        self.title = title
        self.message = message
    }

    init(_ title: String, _ error: Error) {
        self.title = title
        switch error {
        case let e as LinkError: message = e.message
        case let e as ProtocolError: message = e.description
        case let e as BookConversionError: message = e.description
        case let e as ZipError: message = e.description
        case let e as UpdateError: message = e.message
        default: message = "\(error.localizedDescription) Try again. If it continues, restart the device."
        }
    }
}

enum LinkError: Error, Equatable {
    case notConnected
    case notReady
    case timeout
    case missingCharacteristic(String)
    case gatt(String)
    case rejected(String)

    var message: String {
        switch self {
        case .notConnected:
            return "The device is not connected. Keep it near the phone and wait for it to connect."
        case .notReady:
            return "The device is still connecting. Wait a few seconds, then try again."
        case .timeout:
            return "The device did not answer in time. Keep it near the phone and try again."
        case .missingCharacteristic(let name):
            return "The device firmware does not support \(name). Update the device."
        case .gatt(let detail):
            return "Bluetooth reported an error (\(detail)). Try again. If it continues, turn Bluetooth off and on."
        case .rejected(let detail):
            return detail
        }
    }
}

enum UpdateError: Error, Equatable {
    case notConfigured
    case badSignature
    case badSize
    case badHash
    case battery
    case download(String)
    case upload(String)

    var message: String {
        switch self {
        case .notConfigured: return "Updates are not set up in this build of the app. Install the release version from the App Store."
        case .badSignature: return "The update file is not signed by OnePage. The app did not install it. Try again later."
        case .badSize, .badHash: return "The update download is damaged. The app did not install it. Try again on a stable network."
        case .battery: return "The device battery is below 30%. Connect the charger, then try again."
        case .download(let d): return "The update did not download (\(d)). Check the internet connection, then try again."
        case .upload(let d): return "The update did not finish (\(d)). The device stays on its old version. Keep the phone near the device and try again."
        }
    }
}

/// App-side preferences, stored in UserDefaults.
enum Prefs {
    private static let d = UserDefaults.standard

    static var pairedPeripheralID: UUID? {
        get { d.string(forKey: "pairedPeripheralID").flatMap(UUID.init(uuidString:)) }
        set { d.set(newValue?.uuidString, forKey: "pairedPeripheralID") }
    }

    static var pairedDeviceID: String? {
        get { d.string(forKey: "pairedDeviceID") }
        set { d.set(newValue, forKey: "pairedDeviceID") }
    }

    static var onboardingDone: Bool {
        get { d.bool(forKey: "onboardingDone") }
        set { d.set(newValue, forKey: "onboardingDone") }
    }

    static var calendarSyncOn: Bool {
        get { d.bool(forKey: "calendarSyncOn") }
        set { d.set(newValue, forKey: "calendarSyncOn") }
    }

    static var selectedCalendarIDs: Set<String>? {
        get { (d.array(forKey: "selectedCalendarIDs") as? [String]).map(Set.init) }
        set { d.set(newValue.map(Array.init), forKey: "selectedCalendarIDs") }
    }

    static var lastCalendarSync: Date? {
        get { d.object(forKey: "lastCalendarSync") as? Date }
        set { d.set(newValue, forKey: "lastCalendarSync") }
    }

    static var weatherOn: Bool {
        get { d.bool(forKey: "weatherOn") }
        set { d.set(newValue, forKey: "weatherOn") }
    }

    static var weatherUsesLocation: Bool {
        get { d.object(forKey: "weatherUsesLocation") as? Bool ?? true }
        set { d.set(newValue, forKey: "weatherUsesLocation") }
    }

    static var weatherCity: String? {
        get { d.string(forKey: "weatherCity") }
        set { d.set(newValue, forKey: "weatherCity") }
    }

    /// Last known coordinate: [latitude, longitude]. Used in the background,
    /// where the app cannot ask for a new location.
    static var weatherCoordinate: [Double]? {
        get { d.array(forKey: "weatherCoordinate") as? [Double] }
        set { d.set(newValue, forKey: "weatherCoordinate") }
    }

    static var weatherPlaceName: String? {
        get { d.string(forKey: "weatherPlaceName") }
        set { d.set(newValue, forKey: "weatherPlaceName") }
    }

    static var useFahrenheit: Bool {
        get { d.object(forKey: "useFahrenheit") as? Bool ?? (Locale.current.measurementSystem == .us) }
        set { d.set(newValue, forKey: "useFahrenheit") }
    }

    static var lastFirmwareCheck: Date? {
        get { d.object(forKey: "lastFirmwareCheck") as? Date }
        set { d.set(newValue, forKey: "lastFirmwareCheck") }
    }

    static var suggestedTitleOnly: Set<String> {
        get { Set(d.array(forKey: "suggestedTitleOnly") as? [String] ?? []) }
        set { d.set(Array(newValue), forKey: "suggestedTitleOnly") }
    }

    /// The firmware version the phone last alerted about, so the
    /// "update available" alert shows once per version.
    static var lastUpdateAlertVersion: String? {
        get { d.string(forKey: "lastUpdateAlertVersion") }
        set { d.set(newValue, forKey: "lastUpdateAlertVersion") }
    }

    static var lowBatteryAlerted: Bool {
        get { d.bool(forKey: "lowBatteryAlerted") }
        set { d.set(newValue, forKey: "lowBatteryAlerted") }
    }

    static func clearPairing() {
        pairedPeripheralID = nil
        pairedDeviceID = nil
        lowBatteryAlerted = false
    }
}

/// One reply at a time from a notify characteristic. Replies that arrive
/// before anyone waits are kept, so a fast notification is never lost.
/// Used on the main thread only.
final class Mailbox<T> {
    private var items: [T] = []
    private var waiter: CheckedContinuation<T, Error>?
    private var timeoutWork: DispatchWorkItem?

    func push(_ value: T) {
        if let w = waiter {
            waiter = nil
            timeoutWork?.cancel()
            w.resume(returning: value)
        } else {
            items.append(value)
            if items.count > 64 { items.removeFirst() }
        }
    }

    func drain() -> [T] {
        defer { items.removeAll() }
        return items
    }

    func clear() { items.removeAll() }

    func fail(_ error: Error) {
        items.removeAll()
        timeoutWork?.cancel()
        if let w = waiter {
            waiter = nil
            w.resume(throwing: error)
        }
    }

    @MainActor
    func next(timeout: TimeInterval) async throws -> T {
        if !items.isEmpty { return items.removeFirst() }
        if let old = waiter {
            waiter = nil
            old.resume(throwing: LinkError.timeout)
        }
        return try await withCheckedThrowingContinuation { (c: CheckedContinuation<T, Error>) in
            waiter = c
            let work = DispatchWorkItem { [weak self] in
                guard let self, let w = self.waiter else { return }
                self.waiter = nil
                w.resume(throwing: LinkError.timeout)
            }
            timeoutWork = work
            DispatchQueue.main.asyncAfter(deadline: .now() + timeout, execute: work)
        }
    }
}

extension Date {
    var shortRelative: String {
        let f = RelativeDateTimeFormatter()
        f.unitsStyle = .full
        return f.localizedString(for: self, relativeTo: Date())
    }
}

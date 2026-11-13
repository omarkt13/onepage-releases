import Foundation

// MARK: - UUIDs (protocol section 2)

public enum OnePageUUID {
    public static func string(_ short: UInt16) -> String {
        String(format: "4F50%04X-6D69-6E69-8000-6F6E65706167", short)
    }

    public static let service = string(0x0001)
    public static let status = string(0x0002)
    public static let settings = string(0x0003)
    public static let appFilter = string(0x0004)
    public static let homeData = string(0x0005)
    public static let calendarSync = string(0x0006)
    public static let dataRequest = string(0x0007)
    public static let privacy = string(0x0008)
    public static let transferControl = string(0x0009)
    public static let transferData = string(0x000A)

    public static let smpService = "8D53DC1D-1DB7-4CD3-868B-8A527460AA84"
    public static let batteryService = "180F"
    public static let deviceInfoService = "180A"
}

// MARK: - Error codes (protocol section 7)

public enum DeviceErrorCode: UInt16, CaseIterable {
    case none = 0x0000
    case unsupportedVersion = 0x0001
    case malformed = 0x0002
    case storageFull = 0x0003
    case storageFault = 0x0004
    case calendarRejected = 0x0005
    case bookTransferFailed = 0x0006
    case displayFault = 0x0007
    case touchFault = 0x0008

    /// What happened, and what the user does next.
    public var message: String {
        switch self {
        case .none: return "No problem."
        case .unsupportedVersion: return "The device and the app use different versions. Update the app, then update the device."
        case .malformed: return "The device did not understand a message. Disconnect, then connect again."
        case .storageFull: return "The device storage is full. Delete a book in Library."
        case .storageFault: return "The device storage reported a fault. Restart the device. If it continues, contact support."
        case .calendarRejected: return "The device did not accept the calendar. Tap Sync now to try again."
        case .bookTransferFailed: return "The book transfer failed. Keep the phone near the device and try again."
        case .displayFault: return "The device screen reported a fault. Restart the device. If it continues, contact support."
        case .touchFault: return "The touch strip reported a fault. Restart the device. If it continues, contact support."
        }
    }
}

// MARK: - Status (5.1)

public enum ChargingState: UInt8 {
    case notCharging = 0, charging = 1, full = 2, fault = 3

    public var label: String {
        switch self {
        case .notCharging: return "Not charging"
        case .charging: return "Charging"
        case .full: return "Fully charged"
        case .fault: return "Charging fault"
        }
    }
}

public struct DeviceStatus: Equatable {
    public var battery: UInt8?
    public var charging: ChargingState?
    public var firmware: String?
    public var storageFreeKiB: UInt32?
    public var storageTotalKiB: UInt32?
    public var lastError: UInt16?
    public var calendarVersion: UInt32?
    public var calendarSyncedAt: UInt32?
    public var hardwareRevision: UInt8?

    public init() {}

    public init(bytes: [UInt8]) throws {
        for r in try TLV.decode(bytes) {
            switch r.tag {
            case 0x01: battery = r.value.first
            case 0x02: charging = r.value.first.flatMap(ChargingState.init(rawValue:))
            case 0x03: firmware = String(decoding: r.value, as: UTF8.self)
            case 0x04: storageFreeKiB = LE.u32(r.value)
            case 0x05: storageTotalKiB = LE.u32(r.value)
            case 0x06: lastError = LE.u16(r.value)
            case 0x07: calendarVersion = LE.u32(r.value)
            case 0x08: calendarSyncedAt = LE.u32(r.value)
            case 0x09: hardwareRevision = r.value.first
            default: break
            }
        }
    }

    /// Merges a newer partial status into this one.
    public mutating func merge(_ other: DeviceStatus) {
        battery = other.battery ?? battery
        charging = other.charging ?? charging
        firmware = other.firmware ?? firmware
        storageFreeKiB = other.storageFreeKiB ?? storageFreeKiB
        storageTotalKiB = other.storageTotalKiB ?? storageTotalKiB
        lastError = other.lastError ?? lastError
        calendarVersion = other.calendarVersion ?? calendarVersion
        calendarSyncedAt = other.calendarSyncedAt ?? calendarSyncedAt
        hardwareRevision = other.hardwareRevision ?? hardwareRevision
    }

    public func encode() -> [UInt8] {
        var rec: [TLVRecord] = []
        if let v = battery { rec.append(.init(0x01, TLV.u8(v))) }
        if let v = charging { rec.append(.init(0x02, TLV.u8(v.rawValue))) }
        if let v = firmware { rec.append(.init(0x03, Array(v.utf8))) }
        if let v = storageFreeKiB { rec.append(.init(0x04, TLV.u32(v))) }
        if let v = storageTotalKiB { rec.append(.init(0x05, TLV.u32(v))) }
        if let v = lastError { rec.append(.init(0x06, TLV.u16(v))) }
        if let v = calendarVersion { rec.append(.init(0x07, TLV.u32(v))) }
        if let v = calendarSyncedAt { rec.append(.init(0x08, TLV.u32(v))) }
        if let v = hardwareRevision { rec.append(.init(0x09, TLV.u8(v))) }
        return TLV.encode(rec)
    }
}

// MARK: - Settings (5.2)

public enum RefreshMode: UInt8, CaseIterable, Identifiable {
    case quality = 0, balanced = 1, fast = 2
    public var id: UInt8 { rawValue }
    public var label: String {
        switch self {
        case .quality: return "Quality"
        case .balanced: return "Balanced"
        case .fast: return "Fast"
        }
    }
}

public enum AlertStrength: UInt8, CaseIterable, Identifiable {
    case off = 0, low = 1, medium = 2, high = 3
    public var id: UInt8 { rawValue }
    public var label: String {
        switch self {
        case .off: return "Off"
        case .low: return "Low"
        case .medium: return "Medium"
        case .high: return "High"
        }
    }
}

public struct DeviceSettings: Equatable {
    public var quietHoursOn: Bool?
    public var quietStartMinutes: UInt16?
    public var quietEndMinutes: UInt16?
    public var refreshMode: RefreshMode?
    public var clock12Hour: Bool?
    public var language: String?
    public var alertStrength: AlertStrength?

    public init() {}

    public static let defaults: DeviceSettings = {
        var s = DeviceSettings()
        s.quietHoursOn = false
        s.quietStartMinutes = 1320
        s.quietEndMinutes = 420
        s.refreshMode = .balanced
        s.clock12Hour = false
        s.language = "en"
        s.alertStrength = .medium
        return s
    }()

    public init(bytes: [UInt8]) throws {
        for r in try TLV.decode(bytes) {
            switch r.tag {
            case 0x01: quietHoursOn = r.value.first.map { $0 != 0 }
            case 0x02: quietStartMinutes = LE.u16(r.value)
            case 0x03: quietEndMinutes = LE.u16(r.value)
            case 0x04: refreshMode = r.value.first.flatMap(RefreshMode.init(rawValue:))
            case 0x05: clock12Hour = r.value.first.map { $0 != 0 }
            case 0x06: language = r.value.count == 2 ? String(decoding: r.value, as: UTF8.self) : nil
            case 0x07: alertStrength = r.value.first.flatMap(AlertStrength.init(rawValue:))
            default: break
            }
        }
    }

    /// Encodes only the fields that are set, in tag order.
    public func encode() -> [UInt8] {
        var rec: [TLVRecord] = []
        if let v = quietHoursOn { rec.append(.init(0x01, TLV.u8(v ? 1 : 0))) }
        if let v = quietStartMinutes { rec.append(.init(0x02, TLV.u16(v))) }
        if let v = quietEndMinutes { rec.append(.init(0x03, TLV.u16(v))) }
        if let v = refreshMode { rec.append(.init(0x04, TLV.u8(v.rawValue))) }
        if let v = clock12Hour { rec.append(.init(0x05, TLV.u8(v ? 1 : 0))) }
        if let v = language, v.utf8.count == 2 { rec.append(.init(0x06, Array(v.utf8))) }
        if let v = alertStrength { rec.append(.init(0x07, TLV.u8(v.rawValue))) }
        return TLV.encode(rec)
    }

    public mutating func merge(_ o: DeviceSettings) {
        quietHoursOn = o.quietHoursOn ?? quietHoursOn
        quietStartMinutes = o.quietStartMinutes ?? quietStartMinutes
        quietEndMinutes = o.quietEndMinutes ?? quietEndMinutes
        refreshMode = o.refreshMode ?? refreshMode
        clock12Hour = o.clock12Hour ?? clock12Hour
        language = o.language ?? language
        alertStrength = o.alertStrength ?? alertStrength
    }
}

// MARK: - Home data (5.4)

public struct HomeData: Equatable {
    public var tempNowDeci: Int16?
    public var tempHighDeci: Int16?
    public var tempLowDeci: Int16?
    public var condition: WeatherCode?
    public var validUntil: UInt32?
    public var place: String?
    public var fahrenheit: Bool?

    public init() {}

    public func encode() -> [UInt8] {
        var rec: [TLVRecord] = []
        if let v = tempNowDeci { rec.append(.init(0x01, TLV.i16(v))) }
        if let v = tempHighDeci { rec.append(.init(0x02, TLV.i16(v))) }
        if let v = tempLowDeci { rec.append(.init(0x03, TLV.i16(v))) }
        if let v = condition { rec.append(.init(0x04, TLV.u8(v.rawValue))) }
        if let v = validUntil { rec.append(.init(0x05, TLV.u32(v))) }
        if let v = place { rec.append(.init(0x06, Array(TextRules.truncateUTF8(v, maxBytes: 24).utf8))) }
        if let v = fahrenheit { rec.append(.init(0x07, TLV.u8(v ? 1 : 0))) }
        return TLV.encode(rec)
    }

    public init(bytes: [UInt8]) throws {
        for r in try TLV.decode(bytes) {
            switch r.tag {
            case 0x01: tempNowDeci = LE.i16(r.value)
            case 0x02: tempHighDeci = LE.i16(r.value)
            case 0x03: tempLowDeci = LE.i16(r.value)
            case 0x04: condition = r.value.first.flatMap(WeatherCode.init(rawValue:))
            case 0x05: validUntil = LE.u32(r.value)
            case 0x06: place = String(decoding: r.value, as: UTF8.self)
            case 0x07: fahrenheit = r.value.first.map { $0 != 0 }
            default: break
            }
        }
    }

    /// Converts a temperature in °C to tenths of a degree, clamped to Int16.
    public static func deci(_ celsius: Double) -> Int16 {
        let v = (celsius * 10).rounded()
        return Int16(max(Double(Int16.min), min(Double(Int16.max), v)))
    }
}

// MARK: - Data request (5.6)

public struct DataRequest: Equatable {
    public enum Kind: UInt8 { case weather = 1, calendar = 2, both = 3 }
    public enum Reason: UInt8 { case schedule = 0, reconnected = 1, expired = 2, user = 3 }

    public var kind: Kind
    public var reason: Reason?

    public var wantsWeather: Bool { kind == .weather || kind == .both }
    public var wantsCalendar: Bool { kind == .calendar || kind == .both }

    public init(kind: Kind, reason: Reason?) {
        self.kind = kind
        self.reason = reason
    }

    public init(bytes: [UInt8]) throws {
        var r = ByteReader(bytes)
        let version = try r.u8()
        guard version == protocolVersion else { throw ProtocolError.unsupportedVersion(version) }
        let k = try r.u8()
        guard let kind = Kind(rawValue: k) else { throw ProtocolError.invalidValue("data request kind \(k)") }
        self.kind = kind
        if r.isAtEnd {
            self.reason = nil
        } else {
            let raw = try r.u8()
            self.reason = Reason(rawValue: raw)
        }
    }
}

// MARK: - Privacy (5.7)

public struct PrivacySettings: Equatable {
    public var showCalendarWhenAway: Bool?

    public init(showCalendarWhenAway: Bool? = nil) {
        self.showCalendarWhenAway = showCalendarWhenAway
    }

    public init(bytes: [UInt8]) throws {
        for r in try TLV.decode(bytes) where r.tag == 0x01 {
            showCalendarWhenAway = r.value.first.map { $0 != 0 }
        }
    }

    public func encode() -> [UInt8] {
        var rec: [TLVRecord] = []
        if let v = showCalendarWhenAway { rec.append(.init(0x01, TLV.u8(v ? 1 : 0))) }
        return TLV.encode(rec)
    }

    /// Erases calendar, weather, settings, app filter and books, and removes the bond.
    public static let eraseCommand: [UInt8] = TLV.encode([TLVRecord(0xE0, Array("ERAS".utf8))])
}

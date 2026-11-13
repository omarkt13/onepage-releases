import Foundation

// App filter characteristic (protocol section 5.3).

public enum FilterMode: UInt8, CaseIterable, Identifiable, Codable, Sendable {
    case show = 0, silent = 1, hide = 2
    public var id: UInt8 { rawValue }
    public var label: String {
        switch self {
        case .show: return "Show"
        case .silent: return "Silent"
        case .hide: return "Hide"
        }
    }

    public var explanation: String {
        switch self {
        case .show: return "Shows on the device with the alert."
        case .silent: return "Shows on the device with no light or vibration."
        case .hide: return "Does not show on the device."
        }
    }
}

public enum AlertPattern: UInt8, CaseIterable, Identifiable, Codable, Sendable {
    case `default` = 0, single = 1, double = 2, long = 3, none = 4
    public var id: UInt8 { rawValue }
    public var label: String {
        switch self {
        case .default: return "Default"
        case .single: return "Single"
        case .double: return "Double"
        case .long: return "Long"
        case .none: return "None"
        }
    }
}

public struct AppFilterEntry: Equatable, Identifiable, Codable, Sendable {
    public static let maxIdentifierBytes = 64
    public static let titleOnlyFlag: UInt8 = 0x01

    public var mode: FilterMode
    public var pattern: AlertPattern
    public var flags: UInt8
    public var identifier: String

    public var id: String { identifier }

    public var titleOnly: Bool {
        get { flags & Self.titleOnlyFlag != 0 }
        set { flags = newValue ? (flags | Self.titleOnlyFlag) : (flags & ~Self.titleOnlyFlag) }
    }

    public init(mode: FilterMode = .show, pattern: AlertPattern = .default, flags: UInt8 = 0, identifier: String) {
        self.mode = mode
        self.pattern = pattern
        self.flags = flags
        self.identifier = identifier
    }

    public func encode(into w: inout ByteWriter) {
        let id = Array(identifier.utf8.prefix(Self.maxIdentifierBytes))
        w.u8(mode.rawValue)
        w.u8(pattern.rawValue)
        w.u8(flags)
        w.u8(UInt8(id.count))
        w.append(id)
    }

    public func encode() -> [UInt8] {
        var w = ByteWriter()
        encode(into: &w)
        return w.bytes
    }

    public static func decode(_ r: inout ByteReader) throws -> AppFilterEntry {
        let m = try r.u8()
        let p = try r.u8()
        let f = try r.u8()
        let n = Int(try r.u8())
        let id = String(decoding: try r.take(n), as: UTF8.self)
        return AppFilterEntry(mode: FilterMode(rawValue: m) ?? .show,
                              pattern: AlertPattern(rawValue: p) ?? .default,
                              flags: f, identifier: id)
    }

    /// Suggested "title only" during set-up: banking and sign-in code apps.
    public var looksSensitive: Bool {
        let s = identifier.lowercased()
        let hints = ["bank", "authenticator", "auth", "2fa", "otp", "authy", "paypal", "revolut",
                     "monzo", "wise", "chase", "wellsfargo", "hsbc", "barclays", "lloyds", "santander",
                     "password", "1password", "bitwarden", "lastpass", "duo", "okta", "coinbase"]
        return hints.contains { s.contains($0) }
    }
}

public enum AppFilterCommand {
    case list(start: UInt16)
    case set(AppFilterEntry)
    case remove(identifier: String)

    public func encode() -> [UInt8] {
        var w = ByteWriter()
        w.u8(protocolVersion)
        switch self {
        case .list(let start):
            w.u8(0x01)
            w.u16(start)
        case .set(let entry):
            w.u8(0x02)
            entry.encode(into: &w)
        case .remove(let id):
            let b = Array(id.utf8.prefix(AppFilterEntry.maxIdentifierBytes))
            w.u8(0x03)
            w.u8(UInt8(b.count))
            w.append(b)
        }
        return w.bytes
    }
}

public enum AppFilterReply: Equatable {
    case list(total: UInt16, start: UInt16, entries: [AppFilterEntry])
    case setResult(UInt8)
    case removeResult(UInt8)

    public init(bytes: [UInt8]) throws {
        var r = ByteReader(bytes)
        let op = try readHeader(&r)
        switch op {
        case 0x81:
            let total = try r.u16()
            let start = try r.u16()
            let count = Int(try r.u8())
            var entries: [AppFilterEntry] = []
            for _ in 0..<count { entries.append(try AppFilterEntry.decode(&r)) }
            self = .list(total: total, start: start, entries: entries)
        case 0x82: self = .setResult(try r.u8())
        case 0x83: self = .removeResult(try r.u8())
        default: throw ProtocolError.unexpectedOpcode(op)
        }
    }
}

/// Turns bundle identifiers into names. iOS gives no way to look up other
/// apps offline, and the app makes no network calls for this, so it uses a
/// built-in table and falls back to the identifier.
public enum AppNames {
    static let known: [String: String] = [
        "com.apple.MobileSMS": "Messages",
        "com.apple.mobilephone": "Phone",
        "com.apple.mobilemail": "Mail",
        "com.apple.mobilecal": "Calendar",
        "com.apple.reminders": "Reminders",
        "com.apple.facetime": "FaceTime",
        "com.apple.news": "News",
        "com.apple.Health": "Health",
        "com.apple.findmy": "Find My",
        "com.apple.Passbook": "Wallet",
        "com.apple.Home": "Home",
        "com.apple.shortcuts": "Shortcuts",
        "com.apple.weather": "Weather",
        "com.apple.podcasts": "Podcasts",
        "com.apple.Music": "Music",
        "com.apple.mobileslideshow": "Photos",
        "com.apple.AppStore": "App Store",
        "com.apple.mobilesafari": "Safari",
        "com.apple.Fitness": "Fitness",
        "net.whatsapp.WhatsApp": "WhatsApp",
        "ph.telegra.Telegraph": "Telegram",
        "org.whispersystems.signal": "Signal",
        "com.facebook.Messenger": "Messenger",
        "com.burbn.instagram": "Instagram",
        "com.facebook.Facebook": "Facebook",
        "com.atebits.Tweetie2": "X",
        "com.tinyspeck.chatlyio": "Slack",
        "com.microsoft.skype.teams": "Teams",
        "com.microsoft.Office.Outlook": "Outlook",
        "com.google.Gmail": "Gmail",
        "com.google.calendar": "Google Calendar",
        "com.hammerandchisel.discord": "Discord",
        "us.zoom.videomeetings": "Zoom",
        "com.linkedin.LinkedIn": "LinkedIn",
        "com.spotify.client": "Spotify",
        "com.ubercab.UberClient": "Uber",
        "com.amazon.Amazon": "Amazon",
        "com.google.ios.youtube": "YouTube",
        "com.toyopagroup.picaboo": "Snapchat",
        "com.zhiliaoapp.musically": "TikTok",
    ]

    public static func displayName(for identifier: String) -> String {
        if let n = known[identifier] { return n }
        return identifier
    }

    public static func isKnown(_ identifier: String) -> Bool { known[identifier] != nil }
}

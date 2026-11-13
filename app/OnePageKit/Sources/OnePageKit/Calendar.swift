import Foundation

// MARK: - Text rules (protocol 5.5)

public enum TextRules {
    public static let ellipsis: Unicode.Scalar = "\u{2026}"

    /// Cuts `text` to at most `maxChars` Unicode scalars and `maxBytes` UTF-8
    /// bytes. When it cuts, it keeps `maxChars - 1` scalars (fewer if the byte
    /// limit needs it) and adds an ellipsis.
    public static func truncate(_ text: String, maxChars: Int, maxBytes: Int) -> String {
        let scalars = Array(text.unicodeScalars)
        if scalars.count <= maxChars && text.utf8.count <= maxBytes { return text }
        var keep = Array(scalars.prefix(max(0, maxChars - 1)))
        let ellipsisBytes = 3
        var bytes = keep.reduce(0) { $0 + utf8Length($1) }
        while bytes + ellipsisBytes > maxBytes, let last = keep.popLast() {
            bytes -= utf8Length(last)
        }
        var out = String.UnicodeScalarView()
        out.append(contentsOf: keep)
        out.append(ellipsis)
        return String(out)
    }

    /// Cuts to `maxBytes` UTF-8 bytes at a scalar boundary, with no ellipsis.
    public static func truncateUTF8(_ text: String, maxBytes: Int) -> String {
        if text.utf8.count <= maxBytes { return text }
        var out = String.UnicodeScalarView()
        var bytes = 0
        for s in text.unicodeScalars {
            let n = utf8Length(s)
            if bytes + n > maxBytes { break }
            out.append(s)
            bytes += n
        }
        return String(out)
    }

    static func utf8Length(_ s: Unicode.Scalar) -> Int {
        switch s.value {
        case 0..<0x80: return 1
        case 0x80..<0x800: return 2
        case 0x800..<0x10000: return 3
        default: return 4
        }
    }
}

// MARK: - Calendar blob

public struct CalendarEvent: Equatable, Sendable {
    public var start: UInt32
    public var end: UInt32
    public var allDay: Bool
    public var title: String
    public var place: String

    public init(start: UInt32, end: UInt32, allDay: Bool, title: String, place: String) {
        self.start = start
        self.end = end
        self.allDay = allDay
        self.title = title
        self.place = place
    }
}

public enum CalendarBlob {
    public static let maxEvents = 100
    public static let maxBlobBytes = 16384
    public static let titleChars = 80, titleBytes = 160
    public static let placeChars = 60, placeBytes = 120

    /// Builds the blob. Events are written in the given order, cut to 100,
    /// and cut again so the blob fits in `maxBlobBytes`: when the week has a
    /// lot of long text, the latest events are left out, not the whole week.
    /// Use `prepare` first to sort and filter.
    public static func encode(generatedAt: UInt32, events: [CalendarEvent]) -> [UInt8] {
        var records: [[UInt8]] = []
        var size = 6 // generated_at u32 + count u16
        for e in events.prefix(maxEvents) {
            var w = ByteWriter()
            w.u32(e.start)
            w.u32(e.end)
            w.u8(e.allDay ? 1 : 0)
            w.lstr(TextRules.truncate(e.title, maxChars: titleChars, maxBytes: titleBytes))
            w.lstr(TextRules.truncate(e.place, maxChars: placeChars, maxBytes: placeBytes))
            guard size + w.bytes.count <= maxBlobBytes else { break }
            size += w.bytes.count
            records.append(w.bytes)
        }
        var out = ByteWriter()
        out.u32(generatedAt)
        out.u16(UInt16(records.count))
        for r in records { out.append(r) }
        return out.bytes
    }

    public static func decode(_ bytes: [UInt8]) throws -> (generatedAt: UInt32, events: [CalendarEvent]) {
        var r = ByteReader(bytes)
        let gen = try r.u32()
        let count = Int(try r.u16())
        var events: [CalendarEvent] = []
        for _ in 0..<count {
            let s = try r.u32()
            let e = try r.u32()
            let f = try r.u8()
            let t = try r.lstr()
            let p = try r.lstr()
            events.append(CalendarEvent(start: s, end: e, allDay: f & 1 != 0, title: t, place: p))
        }
        return (gen, events)
    }

    /// Keeps events that end after `now`, sorts by start, and keeps at most 100.
    public static func prepare(_ events: [CalendarEvent], now: UInt32) -> [CalendarEvent] {
        Array(events.filter { $0.end > now }
            .sorted { ($0.start, $0.end, $0.title) < ($1.start, $1.end, $1.title) }
            .prefix(maxEvents))
    }
}

public enum CalendarCommand {
    case begin(version: UInt32, totalLength: UInt32, crc32: UInt32)
    case data(offset: UInt32, bytes: ArraySlice<UInt8>)
    case commit(version: UInt32)
    case clear

    public func encode() -> [UInt8] {
        var w = ByteWriter()
        w.u8(protocolVersion)
        switch self {
        case let .begin(version, total, crc):
            w.u8(0x01); w.u32(version); w.u32(total); w.u32(crc)
        case let .data(offset, bytes):
            w.u8(0x02); w.u32(offset); w.append(bytes)
        case let .commit(version):
            w.u8(0x03); w.u32(version)
        case .clear:
            w.u8(0x04)
        }
        return w.bytes
    }

    /// Bytes of overhead in a DATA write: version, opcode, offset.
    public static let dataOverhead = 6
}

public struct CalendarAck: Equatable {
    public enum Status: UInt8 {
        case ok = 0, outOfOrder = 1, checkFailed = 2, tooLarge = 3, badContent = 4, noBegin = 5

        public var message: String {
            switch self {
            case .ok: return "The calendar is on the device."
            case .outOfOrder: return "Part of the calendar went missing. The app sends it again."
            case .checkFailed: return "The device found an error in the calendar it received. Tap Sync now to try again."
            case .tooLarge: return "The week has too many events for the device. Pick fewer calendars."
            case .badContent: return "The device did not accept the calendar. Update the app and the device."
            case .noBegin: return "The device lost the start of the sync. Tap Sync now to try again."
            }
        }
    }

    public var status: Status
    public var version: UInt32
    public var nextOffset: UInt32

    public init(bytes: [UInt8]) throws {
        var r = ByteReader(bytes)
        let op = try readHeader(&r)
        guard op == 0x80 else { throw ProtocolError.unexpectedOpcode(op) }
        let s = try r.u8()
        guard let st = Status(rawValue: s) else { throw ProtocolError.invalidValue("calendar status \(s)") }
        status = st
        version = try r.u32()
        nextOffset = try r.u32()
    }
}

// MARK: - Local seconds (protocol section 1)

/// Seconds since 2000-01-01 00:00:00 local wall-clock time.
public enum LocalSeconds {
    /// 2000-01-01T00:00:00Z as a Unix time.
    static let epoch2000: Int64 = 946_684_800

    public static func from(_ date: Date, timeZone: TimeZone = .current) -> UInt32 {
        let unix = Int64(date.timeIntervalSince1970.rounded(.down))
        let local = unix + Int64(timeZone.secondsFromGMT(for: date)) - epoch2000
        return UInt32(clamping: max(0, local))
    }

    /// Wall-clock components of a local-seconds value, read in UTC.
    public static func date(_ value: UInt32, timeZone: TimeZone = .current) -> Date {
        let wall = Date(timeIntervalSince1970: TimeInterval(Int64(value) + epoch2000))
        // Remove the offset that applied at that wall-clock moment.
        let offset = timeZone.secondsFromGMT(for: wall.addingTimeInterval(-TimeInterval(timeZone.secondsFromGMT(for: wall))))
        return wall.addingTimeInterval(-TimeInterval(offset))
    }
}

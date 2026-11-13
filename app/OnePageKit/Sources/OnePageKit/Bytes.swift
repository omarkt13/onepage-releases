import Foundation

/// Errors raised while decoding a message from the device.
public enum ProtocolError: Error, Equatable, CustomStringConvertible {
    case truncated
    case unsupportedVersion(UInt8)
    case unexpectedOpcode(UInt8)
    case invalidValue(String)

    public var description: String {
        switch self {
        case .truncated: return "The message from the device is too short."
        case .unsupportedVersion(let v): return "The device uses protocol version \(v), which this app does not support. Update the app."
        case .unexpectedOpcode(let op): return String(format: "The device sent an unknown reply (0x%02X).", op)
        case .invalidValue(let what): return "The device sent a value that is not valid: \(what)."
        }
    }
}

/// Little-endian byte writer.
public struct ByteWriter {
    public private(set) var bytes: [UInt8] = []

    public init() {}

    public mutating func u8(_ v: UInt8) { bytes.append(v) }

    public mutating func u16(_ v: UInt16) {
        bytes.append(UInt8(v & 0xFF))
        bytes.append(UInt8(v >> 8))
    }

    public mutating func i16(_ v: Int16) { u16(UInt16(bitPattern: v)) }

    public mutating func u32(_ v: UInt32) {
        bytes.append(UInt8(v & 0xFF))
        bytes.append(UInt8((v >> 8) & 0xFF))
        bytes.append(UInt8((v >> 16) & 0xFF))
        bytes.append(UInt8(v >> 24))
    }

    public mutating func append<S: Sequence>(_ s: S) where S.Element == UInt8 { bytes.append(contentsOf: s) }

    /// Length-prefixed (u8) UTF-8 string. The caller makes sure it fits in 255 bytes.
    public mutating func lstr(_ s: String) {
        let b = Array(s.utf8.prefix(255))
        u8(UInt8(b.count))
        append(b)
    }
}

/// Little-endian byte reader that throws on short input.
public struct ByteReader {
    public let bytes: [UInt8]
    public private(set) var offset: Int

    public init(_ bytes: [UInt8], offset: Int = 0) {
        self.bytes = bytes
        self.offset = offset
    }

    public var remaining: Int { bytes.count - offset }
    public var isAtEnd: Bool { offset >= bytes.count }

    public mutating func u8() throws -> UInt8 {
        guard remaining >= 1 else { throw ProtocolError.truncated }
        defer { offset += 1 }
        return bytes[offset]
    }

    public mutating func u16() throws -> UInt16 {
        guard remaining >= 2 else { throw ProtocolError.truncated }
        defer { offset += 2 }
        return UInt16(bytes[offset]) | UInt16(bytes[offset + 1]) << 8
    }

    public mutating func i16() throws -> Int16 { Int16(bitPattern: try u16()) }

    public mutating func u32() throws -> UInt32 {
        guard remaining >= 4 else { throw ProtocolError.truncated }
        defer { offset += 4 }
        return UInt32(bytes[offset]) | UInt32(bytes[offset + 1]) << 8
            | UInt32(bytes[offset + 2]) << 16 | UInt32(bytes[offset + 3]) << 24
    }

    public mutating func take(_ n: Int) throws -> [UInt8] {
        guard n >= 0, remaining >= n else { throw ProtocolError.truncated }
        defer { offset += n }
        return Array(bytes[offset..<(offset + n)])
    }

    public mutating func lstr() throws -> String {
        let n = Int(try u8())
        return String(decoding: try take(n), as: UTF8.self)
    }
}

/// Little-endian helpers for fixed values inside a TLV record.
enum LE {
    static func u16(_ b: [UInt8]) -> UInt16? {
        guard b.count >= 2 else { return nil }
        return UInt16(b[0]) | UInt16(b[1]) << 8
    }

    static func u32(_ b: [UInt8]) -> UInt32? {
        guard b.count >= 4 else { return nil }
        return UInt32(b[0]) | UInt32(b[1]) << 8 | UInt32(b[2]) << 16 | UInt32(b[3]) << 24
    }

    static func i16(_ b: [UInt8]) -> Int16? { u16(b).map { Int16(bitPattern: $0) } }
}

public extension Array where Element == UInt8 {
    init?(hex: String) {
        var out: [UInt8] = []
        out.reserveCapacity(hex.count / 2)
        var it = hex.makeIterator()
        while let hi = it.next() {
            guard let lo = it.next(), let h = hi.hexDigitValue, let l = lo.hexDigitValue else { return nil }
            out.append(UInt8(h << 4 | l))
        }
        self = out
    }

    var hexString: String { map { String(format: "%02x", $0) }.joined() }
}

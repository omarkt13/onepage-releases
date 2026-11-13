import Foundation

/// The protocol version this app speaks.
public let protocolVersion: UInt8 = 1

/// One `[tag u8][len u8][value]` record.
public struct TLVRecord: Equatable {
    public var tag: UInt8
    public var value: [UInt8]

    public init(_ tag: UInt8, _ value: [UInt8]) {
        self.tag = tag
        self.value = value
    }
}

public enum TLV {
    /// `[version]` followed by the records, in the given order.
    public static func encode(_ records: [TLVRecord]) -> [UInt8] {
        var w = ByteWriter()
        w.u8(protocolVersion)
        for r in records {
            let v = r.value.prefix(255)
            w.u8(r.tag)
            w.u8(UInt8(v.count))
            w.append(v)
        }
        return w.bytes
    }

    /// Checks the version byte and splits the records. Unknown tags are kept;
    /// the caller ignores the ones it does not know.
    public static func decode(_ bytes: [UInt8]) throws -> [TLVRecord] {
        var r = ByteReader(bytes)
        let version = try r.u8()
        guard version == protocolVersion else { throw ProtocolError.unsupportedVersion(version) }
        var out: [TLVRecord] = []
        while r.remaining >= 2 {
            let tag = try r.u8()
            let len = Int(try r.u8())
            guard r.remaining >= len else { break } // Trailing junk: ignore.
            out.append(TLVRecord(tag, try r.take(len)))
        }
        return out
    }

    static func u8(_ v: UInt8) -> [UInt8] { [v] }
    static func u16(_ v: UInt16) -> [UInt8] { var w = ByteWriter(); w.u16(v); return w.bytes }
    static func i16(_ v: Int16) -> [UInt8] { var w = ByteWriter(); w.i16(v); return w.bytes }
    static func u32(_ v: UInt32) -> [UInt8] { var w = ByteWriter(); w.u32(v); return w.bytes }
}

/// Reads the version byte and the opcode at the start of a non-TLV message.
func readHeader(_ r: inout ByteReader) throws -> UInt8 {
    let version = try r.u8()
    guard version == protocolVersion else { throw ProtocolError.unsupportedVersion(version) }
    return try r.u8()
}

import Foundation

public enum ZipError: Error, Equatable, CustomStringConvertible {
    case notAZip
    case unsupportedMethod(UInt16)
    case encrypted
    case zip64
    case missing(String)
    case corrupt(String)

    public var description: String {
        switch self {
        case .notAZip: return "The file is not a valid EPUB (not a zip archive)."
        case .unsupportedMethod(let m): return "The EPUB uses a compression method (\(m)) the app cannot read."
        case .encrypted: return "The EPUB is encrypted. Books with copy protection cannot be sent."
        case .zip64: return "The EPUB is too large for the app to read."
        case .missing(let name): return "The EPUB has no \(name)."
        case .corrupt(let what): return "The EPUB is damaged (\(what))."
        }
    }
}

/// A minimal zip reader for EPUB files: stored and deflated entries, no zip64.
public struct ZipArchive {
    public struct Entry {
        public let name: String
        let method: UInt16
        let flags: UInt16
        let compressedSize: Int
        let uncompressedSize: Int
        let localHeaderOffset: Int
        let crc: UInt32
    }

    let data: [UInt8]
    public private(set) var entries: [String: Entry] = [:]
    public private(set) var orderedNames: [String] = []

    public init(bytes: [UInt8]) throws {
        data = bytes
        guard bytes.count >= 22 else { throw ZipError.notAZip }
        // Find the end-of-central-directory record, searching back over a comment.
        var eocd = -1
        let lowest = max(0, bytes.count - 22 - 0xFFFF)
        var i = bytes.count - 22
        while i >= lowest {
            if bytes[i] == 0x50 && bytes[i + 1] == 0x4B && bytes[i + 2] == 0x05 && bytes[i + 3] == 0x06 {
                eocd = i
                break
            }
            i -= 1
        }
        guard eocd >= 0 else { throw ZipError.notAZip }
        var r = ByteReader(bytes, offset: eocd + 10)
        let total = Int(try r.u16())
        let cdSize = Int(try r.u32())
        let cdOffset = Int(try r.u32())
        if cdOffset == 0xFFFF_FFFF || total == 0xFFFF { throw ZipError.zip64 }
        guard cdOffset + cdSize <= bytes.count else { throw ZipError.corrupt("directory") }

        var c = ByteReader(bytes, offset: cdOffset)
        for _ in 0..<total {
            guard try c.u32() == 0x0201_4B50 else { throw ZipError.corrupt("directory entry") }
            _ = try c.u16() // version made by
            _ = try c.u16() // version needed
            let flags = try c.u16()
            let method = try c.u16()
            _ = try c.u16(); _ = try c.u16() // time, date
            let crc = try c.u32()
            let csize = Int(try c.u32())
            let usize = Int(try c.u32())
            let nlen = Int(try c.u16())
            let elen = Int(try c.u16())
            let clen = Int(try c.u16())
            _ = try c.u16(); _ = try c.u16(); _ = try c.u32() // disk, internal, external attrs
            let local = Int(try c.u32())
            let name = String(decoding: try c.take(nlen), as: UTF8.self)
            _ = try c.take(elen + clen)
            let e = Entry(name: name, method: method, flags: flags, compressedSize: csize,
                          uncompressedSize: usize, localHeaderOffset: local, crc: crc)
            entries[name] = e
            orderedNames.append(name)
        }
    }

    public func contains(_ name: String) -> Bool { entries[name] != nil }

    public func read(_ name: String) throws -> [UInt8] {
        guard let e = entries[name] else { throw ZipError.missing(name) }
        if e.flags & 0x0001 != 0 { throw ZipError.encrypted }
        var r = ByteReader(data, offset: e.localHeaderOffset)
        guard try r.u32() == 0x0403_4B50 else { throw ZipError.corrupt("local header") }
        var h = ByteReader(data, offset: e.localHeaderOffset + 26)
        let nlen = Int(try h.u16())
        let elen = Int(try h.u16())
        let start = e.localHeaderOffset + 30 + nlen + elen
        guard start + e.compressedSize <= data.count else { throw ZipError.corrupt(name) }
        let raw = Array(data[start..<(start + e.compressedSize)])
        let out: [UInt8]
        switch e.method {
        case 0: out = raw
        case 8: out = try Inflate.inflate(raw, expectedSize: e.uncompressedSize)
        default: throw ZipError.unsupportedMethod(e.method)
        }
        guard CRC32.checksum(out) == e.crc else { throw ZipError.corrupt(name) }
        return out
    }

    public func readString(_ name: String) throws -> String {
        String(decoding: try read(name), as: UTF8.self)
    }
}

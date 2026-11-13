import Foundation

// Transfer control and data (protocol sections 5.8 and 5.9).

public enum TransferCommand {
    case start(bookID: UInt32, totalLength: UInt32, crc32: UInt32, title: String)
    case query(bookID: UInt32)
    case cancel(bookID: UInt32)
    case delete(bookID: UInt32)
    case list(start: UInt16)
    case finish(bookID: UInt32)
    case fast(Bool)

    public func encode() -> [UInt8] {
        var w = ByteWriter()
        w.u8(protocolVersion)
        switch self {
        case let .start(id, total, crc, title):
            w.u8(0x01); w.u32(id); w.u32(total); w.u32(crc)
            w.lstr(TextRules.truncateUTF8(title, maxBytes: 64))
        case let .query(id): w.u8(0x02); w.u32(id)
        case let .cancel(id): w.u8(0x03); w.u32(id)
        case let .delete(id): w.u8(0x04); w.u32(id)
        case let .list(start): w.u8(0x05); w.u16(start)
        case let .finish(id): w.u8(0x06); w.u32(id)
        case let .fast(on): w.u8(0x07); w.u8(on ? 1 : 0)
        }
        return w.bytes
    }
}

public enum TransferStatus: UInt8, Sendable {
    case ok = 0, badBlock = 1, crcMismatch = 2, noSpace = 3, unknownBook = 4, busy = 5, storageError = 6

    public var message: String {
        switch self {
        case .ok: return "Done."
        case .badBlock: return "Some data went missing. The app sends it again."
        case .crcMismatch: return "The book arrived with errors. Send it again."
        case .noSpace: return "The device has no space for this book. Delete a book, then try again."
        case .unknownBook: return "The device does not have this book. Send it again."
        case .busy: return "The device is receiving another book. Wait for it to finish."
        case .storageError: return "The device storage failed. Restart the device, then try again."
        }
    }
}

public struct DeviceBook: Equatable, Identifiable, Sendable {
    public var bookID: UInt32
    public var size: UInt32
    public var position: UInt32
    public var title: String

    public var id: UInt32 { bookID }
    public var progress: Double { size == 0 ? 0 : min(1, Double(position) / Double(size)) }

    public init(bookID: UInt32, size: UInt32, position: UInt32, title: String) {
        self.bookID = bookID
        self.size = size
        self.position = position
        self.title = title
    }
}

public enum TransferReply: Equatable {
    case progress(bookID: UInt32, status: TransferStatus, nextOffset: UInt32)
    case list(total: UInt16, start: UInt16, books: [DeviceBook])
    case result(bookID: UInt32, status: TransferStatus)

    public init(bytes: [UInt8]) throws {
        var r = ByteReader(bytes)
        let op = try readHeader(&r)
        func status() throws -> TransferStatus {
            let s = try r.u8()
            guard let st = TransferStatus(rawValue: s) else { throw ProtocolError.invalidValue("transfer status \(s)") }
            return st
        }
        switch op {
        case 0x81:
            let id = try r.u32()
            let st = try status()
            self = .progress(bookID: id, status: st, nextOffset: try r.u32())
        case 0x85:
            let total = try r.u16()
            let start = try r.u16()
            let count = Int(try r.u8())
            var books: [DeviceBook] = []
            for _ in 0..<count {
                let id = try r.u32()
                let size = try r.u32()
                let pos = try r.u32()
                books.append(DeviceBook(bookID: id, size: size, position: pos, title: try r.lstr()))
            }
            self = .list(total: total, start: start, books: books)
        case 0x86:
            let id = try r.u32()
            self = .result(bookID: id, status: try status())
        default:
            throw ProtocolError.unexpectedOpcode(op)
        }
    }
}

public enum TransferBlock {
    /// version, offset u32, crc16 u16
    public static let overhead = 7

    public static func encode(offset: UInt32, data: ArraySlice<UInt8>) -> [UInt8] {
        var w = ByteWriter()
        w.u8(protocolVersion)
        w.u32(offset)
        w.u16(CRC16.checksum(data))
        w.append(data)
        return w.bytes
    }
}

/// Splits a file into blocks and follows the device's resume offsets.
/// It holds no Bluetooth state, so it can be tested alone.
public struct TransferPlan {
    public let bookID: UInt32
    public let file: [UInt8]
    public let blockSize: Int
    public private(set) var nextOffset: Int = 0
    /// The offset the device last confirmed. The device is the authority: if it
    /// discarded a partial book (a new START replaces it), this goes back too.
    public private(set) var confirmedOffset: Int = 0

    public init(bookID: UInt32, file: [UInt8], maxWriteLength: Int) {
        self.bookID = bookID
        self.file = file
        self.blockSize = max(1, maxWriteLength - TransferBlock.overhead)
    }

    public var totalLength: Int { file.count }
    public var crc32: UInt32 { CRC32.checksum(file) }
    public var allSent: Bool { nextOffset >= file.count }
    public var fractionConfirmed: Double { file.isEmpty ? 1 : Double(confirmedOffset) / Double(file.count) }

    /// The next block to send, or nil when every byte is sent once.
    public mutating func nextBlock() -> [UInt8]? {
        guard nextOffset < file.count else { return nil }
        let end = min(file.count, nextOffset + blockSize)
        let block = TransferBlock.encode(offset: UInt32(nextOffset), data: file[nextOffset..<end])
        nextOffset = end
        return block
    }

    /// Applies the device's PROGRESS reply. Returns true if the app must
    /// resend from an earlier offset.
    @discardableResult
    public mutating func apply(deviceOffset: UInt32) -> Bool {
        let off = min(Int(deviceOffset), file.count)
        confirmedOffset = off
        if off < nextOffset {
            nextOffset = off
            return true
        }
        if off > nextOffset { nextOffset = off }
        return false
    }

    /// Applies a routine PROGRESS (status OK) that arrives while blocks are
    /// still in flight. It confirms bytes but never rewinds: the device
    /// reports every 32 blocks, and the app is normally ahead of that offset
    /// by the blocks still queued. A rewind here would resend every block in
    /// flight. Only a START or QUERY reply, or a bad-block PROGRESS, rewinds
    /// (use `apply(deviceOffset:)` for those).
    public mutating func confirm(deviceOffset: UInt32) {
        let off = min(Int(deviceOffset), file.count)
        confirmedOffset = max(confirmedOffset, off)
        if off > nextOffset { nextOffset = off }
    }
}

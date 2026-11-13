import Foundation

/// The device's book file (protocol section 6).
public struct OPBBook: Equatable, Sendable {
    public struct Chapter: Equatable, Sendable {
        public var textOffset: UInt32
        public var name: String
        public init(textOffset: UInt32, name: String) {
            self.textOffset = textOffset
            self.name = name
        }
    }

    public var title: String
    public var author: String
    public var chapters: [Chapter]
    /// Windows-1252 text. 0x0A ends a paragraph.
    public var text: [UInt8]

    public init(title: String, author: String, chapters: [Chapter], text: [UInt8]) {
        self.title = title
        self.author = author
        self.chapters = chapters
        self.text = text
    }

    public static let headerSize = 16

    public func encode() -> [UInt8] {
        var meta = ByteWriter()
        meta.lstr(TextRules.truncateUTF8(title, maxBytes: 64))
        meta.lstr(TextRules.truncateUTF8(author, maxBytes: 64))
        var table = ByteWriter()
        let chapterList = Array(chapters.prefix(Int(UInt16.max)))
        for c in chapterList {
            table.u32(c.textOffset)
            table.lstr(TextRules.truncateUTF8(c.name, maxBytes: 48))
        }
        let textOffset = Self.headerSize + meta.bytes.count + table.bytes.count
        var w = ByteWriter()
        w.append(Array("OPB1".utf8))
        w.u8(1) // format version
        w.u8(1) // charset: Windows-1252
        w.u16(UInt16(chapterList.count))
        w.u32(UInt32(textOffset))
        w.u32(UInt32(text.count))
        w.append(meta.bytes)
        w.append(table.bytes)
        w.append(text)
        return w.bytes
    }

    public init(bytes: [UInt8]) throws {
        var r = ByteReader(bytes)
        guard try r.take(4) == Array("OPB1".utf8) else { throw ProtocolError.invalidValue("book magic") }
        _ = try r.u8()
        _ = try r.u8()
        let n = Int(try r.u16())
        let textOff = Int(try r.u32())
        let textLen = Int(try r.u32())
        title = try r.lstr()
        author = try r.lstr()
        var ch: [Chapter] = []
        for _ in 0..<n {
            let off = try r.u32()
            ch.append(Chapter(textOffset: off, name: try r.lstr()))
        }
        chapters = ch
        guard textOff + textLen <= bytes.count else { throw ProtocolError.truncated }
        text = Array(bytes[textOff..<(textOff + textLen)])
    }
}

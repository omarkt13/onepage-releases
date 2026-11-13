import Foundation
import XCTest
@testable import OnePageKit

final class LogicTests: XCTestCase {
    static let fixtures: URL = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent().appendingPathComponent("Fixtures")

    func fixture(_ name: String) throws -> [UInt8] {
        [UInt8](try Data(contentsOf: Self.fixtures.appendingPathComponent(name)))
    }

    // MARK: Inflate and zip

    func testPureInflateDynamicAndStored() throws {
        let expected = try fixture("deflate.txt")
        XCTAssertEqual(try PureInflate.decode(try fixture("deflate.bin")), expected)
        XCTAssertEqual(try PureInflate.decode(try fixture("stored.bin")), Array("hello stored".utf8))
        XCTAssertEqual(try Inflate.inflate(try fixture("deflate.bin"), expectedSize: expected.count), expected)
    }

    func testInflateRejectsGarbage() {
        XCTAssertThrowsError(try PureInflate.decode([0xFF, 0xFF, 0xFF]))
    }

    func testEPUBConversion() throws {
        let book = try BookConverter.convert(fileName: "sample.epub", bytes: try fixture("sample.epub"))
        XCTAssertEqual(book.title, "The Small Test")
        XCTAssertEqual(book.author, "A. Writer")
        XCTAssertEqual(book.chapters.map(\.name), ["Chapter One", "Chapter Two"])
        XCTAssertEqual(book.chapters.first?.textOffset, 0)
        let expected = "Chapter One\nThe \u{201C}quick\u{201D} brown fox \u{2014} na\u{EF}ve caf\u{E9}\u{2026}\n"
            + "Second paragraph across lines.\nChapter Two\nEnd & done.\n"
        XCTAssertEqual(book.text, CP1252.encode(expected))
        let second = Int(book.chapters[1].textOffset)
        XCTAssertEqual(Array(book.text[second..<(second + 11)]), Array("Chapter Two".utf8))
        // Round trip through the file format.
        XCTAssertEqual(try OPBBook(bytes: book.encode()), book)
    }

    func testPlainTextConversion() throws {
        let src = "CHAPTER I\n\nIt was a dark\nand stormy night.\n\nCHAPTER II\n\nThe end.\n"
        let book = try BookConverter.convert(fileName: "story.txt", bytes: Array(src.utf8))
        XCTAssertEqual(book.title, "story")
        XCTAssertEqual(book.chapters.map(\.name), ["CHAPTER I", "CHAPTER II"])
        XCTAssertEqual(String(decoding: book.text, as: UTF8.self),
                       "CHAPTER I\nIt was a dark and stormy night.\nCHAPTER II\nThe end.\n")
    }

    func testUnsupportedFormat() {
        XCTAssertThrowsError(try BookConverter.convert(fileName: "x.pdf", bytes: [1, 2, 3]))
    }

    func testResolveHref() {
        XCTAssertEqual(BookConverter.resolve(base: "OEBPS/text", href: "../images/a%20b.png#x"), "OEBPS/images/a b.png")
        XCTAssertEqual(BookConverter.resolve(base: "", href: "./ch1.xhtml"), "ch1.xhtml")
    }

    // MARK: Transfer plan

    func testTransferPlanResume() {
        let file = (0..<1000).map { UInt8($0 & 0xFF) }
        var plan = TransferPlan(bookID: 1, file: file, maxWriteLength: 107) // 100-byte blocks
        var sent = 0
        while plan.nextBlock() != nil { sent += 1 }
        XCTAssertEqual(sent, 10)
        XCTAssertTrue(plan.allSent)

        // The device lost the block at 300: resend from there.
        XCTAssertTrue(plan.apply(deviceOffset: 300))
        let block = plan.nextBlock()!
        var r = ByteReader(block)
        XCTAssertEqual(try r.u8(), protocolVersion)
        XCTAssertEqual(try r.u32(), 300)
        let crc = try! r.u16()
        XCTAssertEqual(crc, CRC16.checksum(file[300..<400]))
        XCTAssertEqual(plan.fractionConfirmed, 0.3, accuracy: 0.0001)

        // A later confirmation moves forward and does not rewind.
        XCTAssertFalse(plan.apply(deviceOffset: 1000))
        XCTAssertTrue(plan.allSent)
    }

    // MARK: Text, links, weather, versions

    func testTruncateByteLimit() {
        let s = String(repeating: "é", count: 70) // 140 bytes, 70 chars
        // 59 × "é" + "…" is 121 bytes, so one more "é" goes: 58 + ellipsis.
        XCTAssertEqual(TextRules.truncate(s, maxChars: 60, maxBytes: 120).unicodeScalars.count, 59)
        XCTAssertLessThanOrEqual(TextRules.truncate(s, maxChars: 60, maxBytes: 120).utf8.count, 120)
        XCTAssertEqual(TextRules.truncateUTF8("Zürich-Altstetten-Nord-West", maxBytes: 24).utf8.count, 24)
    }

    func testPairingLink() {
        XCTAssertEqual(PairingLink.deviceID(from: URL(string: "https://onepage.app/p/1a2b3c4d")!), "1A2B3C4D")
        XCTAssertNil(PairingLink.deviceID(from: URL(string: "https://evil.example/p/1A2B3C4D")!))
        XCTAssertNil(PairingLink.deviceID(from: URL(string: "https://onepage.app/p/xyz")!))
        XCTAssertNil(PairingLink.deviceID(from: URL(string: "http://onepage.app/p/1A2B3C4D")!))
    }

    func testAdvertInfo() throws {
        let info = try XCTUnwrap(AdvertInfo(manufacturerData: [0xFF, 0xFF, 0x4D, 0x3C, 0x2B, 0x1A, 0x01]))
        XCTAssertTrue(info.pairing)
        XCTAssertFalse(info.hasBond)
        XCTAssertEqual(info.deviceIDString, "1A2B3C4D")
        XCTAssertTrue(DeviceID.matches("1a2b3c4d", info.deviceID))
        XCTAssertTrue(DeviceID.matches("4D3C2B1A", info.deviceID))
        XCTAssertNil(AdvertInfo(manufacturerData: [0x4C, 0x00, 1, 2, 3, 4, 5]))
    }

    func testWeatherMapping() {
        XCTAssertEqual(WeatherCode.fromWeatherKit("partlyCloudy"), .partlyCloudy)
        XCTAssertEqual(WeatherCode.fromWeatherKit("heavySnow"), .snow)
        XCTAssertEqual(WeatherCode.fromWeatherKit("somethingNew"), .unknown)
        XCTAssertEqual(HomeData.deci(-3.54), -35)
    }

    func testSemVer() {
        XCTAssertTrue(SemVer.isNewer("1.10.0", than: "1.9.9"))
        XCTAssertFalse(SemVer.isNewer("1.0.0", than: "1.0.0-rc1"))
        XCTAssertFalse(SemVer.isNewer("1.0", than: "1.0.0"))
    }

    func testLocalSecondsRoundTrip() {
        let tz = TimeZone(identifier: "Europe/Zurich")!
        let date = Date(timeIntervalSince1970: 1_700_000_000) // 2023-11-14T22:13:20Z
        let local = LocalSeconds.from(date, timeZone: tz)
        // 22:13:20Z is 23:13:20 in Zurich (UTC+1).
        XCTAssertEqual(Int64(local), 1_700_000_000 + 3600 - 946_684_800)
        XCTAssertEqual(LocalSeconds.date(local, timeZone: tz), date)
    }

    func testCalendarPrepare() {
        let e = [
            CalendarEvent(start: 50, end: 60, allDay: false, title: "b", place: ""),
            CalendarEvent(start: 10, end: 20, allDay: false, title: "past", place: ""),
            CalendarEvent(start: 5, end: 100, allDay: true, title: "a", place: ""),
        ]
        XCTAssertEqual(CalendarBlob.prepare(e, now: 30).map(\.title), ["a", "b"])
    }
}

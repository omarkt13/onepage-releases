import Foundation
import XCTest
@testable import OnePageKit

/// Checks every vector in shared/test-vectors/vectors.json, which the
/// firmware host tests also read. A failure here means the app and the
/// firmware no longer agree on the protocol.
final class VectorTests: XCTestCase {
    static let repoRoot: URL = {
        var u = URL(fileURLWithPath: #filePath)
        for _ in 0..<5 { u.deleteLastPathComponent() }
        return u
    }()

    var v: [String: Any] = [:]

    override func setUpWithError() throws {
        let url = Self.repoRoot.appendingPathComponent("shared/test-vectors/vectors.json")
        let data = try Data(contentsOf: url)
        v = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
    }

    func dict(_ key: String, in d: [String: Any]? = nil) throws -> [String: Any] {
        try XCTUnwrap((d ?? v)[key] as? [String: Any], "missing \(key)")
    }

    func bytes(_ key: String, in d: [String: Any]) throws -> [UInt8] {
        let s = try XCTUnwrap(d[key] as? String, "missing \(key)")
        return try XCTUnwrap([UInt8](hex: s), "bad hex in \(key)")
    }

    func int(_ key: String, in d: [String: Any]) throws -> Int {
        try XCTUnwrap((d[key] as? NSNumber)?.intValue, "missing \(key)")
    }

    func str(_ key: String, in d: [String: Any]) throws -> String {
        try XCTUnwrap(d[key] as? String, "missing \(key)")
    }

    func testProtocolVersion() throws {
        XCTAssertEqual(try int("protocol_version", in: v), Int(protocolVersion))
    }

    func testCRC() throws {
        let list = try XCTUnwrap(v["crc"] as? [[String: Any]])
        XCTAssertFalse(list.isEmpty)
        for c in list {
            let input = try bytes("input", in: c)
            XCTAssertEqual(Int(CRC32.checksum(input)), try int("crc32", in: c))
            XCTAssertEqual(Int(CRC16.checksum(input)), try int("crc16", in: c))
        }
    }

    func testStatus() throws {
        let s = try dict("status")
        let f = try dict("fields", in: s)
        let raw = try bytes("bytes", in: s)
        let st = try DeviceStatus(bytes: raw)
        XCTAssertEqual(Int(st.battery ?? 0), try int("battery", in: f))
        XCTAssertEqual(Int(st.charging?.rawValue ?? 99), try int("charging", in: f))
        XCTAssertEqual(st.firmware, try str("firmware", in: f))
        XCTAssertEqual(Int(st.storageFreeKiB ?? 0), try int("storage_free_kib", in: f))
        XCTAssertEqual(Int(st.storageTotalKiB ?? 0), try int("storage_total_kib", in: f))
        XCTAssertEqual(Int(st.lastError ?? 99), try int("last_error", in: f))
        XCTAssertEqual(Int(st.calendarVersion ?? 0), try int("calendar_version", in: f))
        XCTAssertEqual(Int(st.calendarSyncedAt ?? 0), try int("calendar_synced_at", in: f))
        XCTAssertEqual(Int(st.hardwareRevision ?? 0), try int("hw_rev", in: f))
        XCTAssertEqual(st.encode(), raw)
    }

    func testSettings() throws {
        let s = try dict("settings")
        let f = try dict("fields", in: s)
        let raw = try bytes("bytes", in: s)
        let d = try DeviceSettings(bytes: raw)
        XCTAssertEqual(d.quietHoursOn, try int("quiet_on", in: f) != 0)
        XCTAssertEqual(Int(d.quietStartMinutes ?? 0), try int("quiet_start", in: f))
        XCTAssertEqual(Int(d.quietEndMinutes ?? 0), try int("quiet_end", in: f))
        XCTAssertEqual(Int(d.refreshMode?.rawValue ?? 99), try int("refresh_mode", in: f))
        XCTAssertEqual(d.clock12Hour, try int("clock_12h", in: f) != 0)
        XCTAssertEqual(d.language, try str("language", in: f))
        XCTAssertEqual(Int(d.alertStrength?.rawValue ?? 99), try int("alert_strength", in: f))
        XCTAssertEqual(d.encode(), raw)

        let partial = try DeviceSettings(bytes: try bytes("partial_with_unknown", in: s))
        let expect = try dict("partial_expect", in: s)
        XCTAssertEqual(Int(partial.alertStrength?.rawValue ?? 99), try int("alert_strength", in: expect))
        XCTAssertNil(partial.quietHoursOn)
        XCTAssertNil(partial.language)
    }

    func entry(_ d: [String: Any]) throws -> AppFilterEntry {
        AppFilterEntry(mode: try XCTUnwrap(FilterMode(rawValue: UInt8(try int("mode", in: d)))),
                       pattern: try XCTUnwrap(AlertPattern(rawValue: UInt8(try int("pattern", in: d)))),
                       flags: UInt8(try int("flags", in: d)),
                       identifier: try str("id", in: d))
    }

    func testAppFilter() throws {
        let a = try dict("app_filter")
        let e = try entry(try dict("entry", in: a))
        XCTAssertTrue(e.titleOnly)
        XCTAssertEqual(e.encode(), try bytes("entry_bytes", in: a))
        XCTAssertEqual(AppFilterCommand.set(e).encode(), try bytes("set_bytes", in: a))
        XCTAssertEqual(AppFilterCommand.list(start: 0).encode(), try bytes("list_request_bytes", in: a))

        let reply = try dict("list_reply", in: a)
        let expected = try XCTUnwrap(reply["entries"] as? [[String: Any]]).map(entry)
        let decoded = try AppFilterReply(bytes: try bytes("bytes", in: reply))
        XCTAssertEqual(decoded, .list(total: UInt16(try int("total", in: reply)),
                                      start: UInt16(try int("start", in: reply)),
                                      entries: expected))
    }

    func testHomeData() throws {
        let h = try dict("home_data")
        let f = try dict("fields", in: h)
        var d = HomeData()
        d.tempNowDeci = Int16(try int("temp_now", in: f))
        d.tempHighDeci = Int16(try int("temp_high", in: f))
        d.tempLowDeci = Int16(try int("temp_low", in: f))
        d.condition = WeatherCode(rawValue: UInt8(try int("condition", in: f)))
        d.validUntil = UInt32(try int("valid_until", in: f))
        d.place = try str("place", in: f)
        d.fahrenheit = try int("unit", in: f) != 0
        let raw = try bytes("bytes", in: h)
        XCTAssertEqual(d.encode(), raw)
        XCTAssertEqual(try HomeData(bytes: raw), d)
    }

    func testCalendarCap() throws {
        let c = try dict("calendar_cap")
        let t = try XCTUnwrap(c["event_template"] as? [String: Any])
        let start = try int("start", in: t), step = try int("step", in: t), length = try int("length", in: t)
        let title = try str("title", in: t), place = try str("place", in: t)
        let events = (0..<(try int("count", in: t))).map { i in
            CalendarEvent(start: UInt32(start + i * step), end: UInt32(start + i * step + length),
                          allDay: false, title: title, place: place)
        }
        let blob = CalendarBlob.encode(generatedAt: UInt32(try int("generated_at", in: c)), events: events)
        XCTAssertEqual(blob.count, try int("blob_len", in: c))
        XCTAssertLessThanOrEqual(blob.count, CalendarBlob.maxBlobBytes)
        XCTAssertEqual(Int(CRC32.checksum(blob)), try int("crc32", in: c))
        XCTAssertEqual(try CalendarBlob.decode(blob).events.count, try int("expected_count", in: c))
    }

    func testCalendar() throws {
        let c = try dict("calendar")
        let input = try XCTUnwrap(c["events_in"] as? [[String: Any]])
        let events = try input.map { e in
            CalendarEvent(start: UInt32(try int("start", in: e)), end: UInt32(try int("end", in: e)),
                          allDay: (e["all_day"] as? Bool) ?? false,
                          title: try str("title", in: e), place: (e["place"] as? String) ?? "")
        }
        let gen = UInt32(try int("generated_at", in: c))
        let blob = CalendarBlob.encode(generatedAt: gen, events: events)
        XCTAssertEqual(blob.hexString, try str("blob", in: c))
        XCTAssertEqual(Int(CRC32.checksum(blob)), try int("crc32", in: c))

        let out = try XCTUnwrap(c["events_out"] as? [[String: Any]])
        let decoded = try CalendarBlob.decode(blob)
        XCTAssertEqual(decoded.generatedAt, gen)
        XCTAssertEqual(decoded.events.count, out.count)
        for (d, o) in zip(decoded.events, out) {
            XCTAssertEqual(d.title, try str("title", in: o))
            XCTAssertEqual(d.place, try str("place", in: o))
            XCTAssertEqual(d.allDay, (o["all_day"] as? Bool) ?? false)
        }

        let crc = CRC32.checksum(blob)
        XCTAssertEqual(CalendarCommand.begin(version: 7, totalLength: UInt32(blob.count), crc32: crc).encode(),
                       try bytes("begin_bytes", in: c))
        XCTAssertEqual(CalendarCommand.data(offset: 0, bytes: blob[0..<100]).encode(),
                       try bytes("data_first_bytes", in: c))
        XCTAssertEqual(CalendarCommand.commit(version: 7).encode(), try bytes("commit_bytes", in: c))
        let ack = try CalendarAck(bytes: try bytes("ack_ok_bytes", in: c))
        XCTAssertEqual(ack.status, .ok)
        XCTAssertEqual(ack.version, 7)
        XCTAssertEqual(Int(ack.nextOffset), blob.count)
    }

    func testTruncate() throws {
        let list = try XCTUnwrap(v["truncate"] as? [[String: Any]])
        for t in list {
            let out = TextRules.truncate(try str("in", in: t),
                                         maxChars: try int("max_chars", in: t),
                                         maxBytes: try int("max_bytes", in: t))
            XCTAssertEqual(out, try str("out", in: t))
        }
    }

    func testDataRequest() throws {
        let list = try XCTUnwrap(v["data_request"] as? [[String: Any]])
        for d in list {
            let r = try DataRequest(bytes: try bytes("bytes", in: d))
            XCTAssertEqual(Int(r.kind.rawValue), try int("kind", in: d))
            XCTAssertEqual(Int(r.reason?.rawValue ?? 99), try int("reason", in: d))
        }
    }

    func testPrivacy() throws {
        let p = try dict("privacy")
        let read = try bytes("read_bytes", in: p)
        XCTAssertEqual(try PrivacySettings(bytes: read).showCalendarWhenAway, true)
        XCTAssertEqual(PrivacySettings(showCalendarWhenAway: true).encode(), read)
        XCTAssertEqual(PrivacySettings.eraseCommand, try bytes("erase_bytes", in: p))
    }

    func testTransfer() throws {
        let t = try dict("transfer")
        XCTAssertEqual(TransferCommand.start(bookID: 0xA1B2_C3D4, totalLength: 70000, crc32: 0x1234_5678, title: "1984").encode(),
                       try bytes("start_bytes", in: t))
        let block = try dict("block", in: t)
        let data = try bytes("data", in: block)
        XCTAssertEqual(Int(CRC16.checksum(data)), try int("crc16", in: block))
        XCTAssertEqual(TransferBlock.encode(offset: UInt32(try int("offset", in: block)), data: data[...]),
                       try bytes("bytes", in: block))
        XCTAssertEqual(try TransferReply(bytes: try bytes("progress_bytes", in: t)),
                       .progress(bookID: 0xA1B2_C3D4, status: .badBlock, nextOffset: 4096))
        XCTAssertEqual(try TransferReply(bytes: try bytes("list_reply_bytes", in: t)),
                       .list(total: 1, start: 0, books: [DeviceBook(bookID: 0xA1B2_C3D4, size: 70000, position: 35000, title: "1984")]))
    }

    func testCP1252AndOPB() throws {
        let c = try dict("cp1252")
        XCTAssertEqual(CP1252.encode(try str("in", in: c)).hexString, try str("out", in: c))

        let o = try dict("opb")
        let chapters = try XCTUnwrap(o["chapters"] as? [[Any]]).map { pair -> OPBBook.Chapter in
            OPBBook.Chapter(textOffset: UInt32((pair[0] as! NSNumber).intValue), name: pair[1] as! String)
        }
        let book = OPBBook(title: try str("title", in: o), author: try str("author", in: o),
                           chapters: chapters, text: CP1252.encode(try str("in", in: c)))
        let encoded = book.encode()
        XCTAssertEqual(encoded.hexString, try str("bytes", in: o))
        XCTAssertEqual(Int(CRC32.checksum(encoded)), try int("crc32", in: o))
        XCTAssertEqual(try OPBBook(bytes: encoded), book)
    }
}

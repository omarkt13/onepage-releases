import OnePageKit
import XCTest
@testable import OnePageMini

final class AppTests: XCTestCase {
    func testMinutesRoundTrip() {
        for m: UInt16 in [0, 420, 1320, 1439] {
            XCTAssertEqual(Minutes.minutes(Minutes.date(m)), m)
        }
    }

    @MainActor
    func testMailboxKeepsEarlyReplies() async throws {
        let box = Mailbox<Int>()
        box.push(1)
        box.push(2)
        let first = try await box.next(timeout: 1)
        XCTAssertEqual(first, 1)
        XCTAssertEqual(box.drain(), [2])
    }

    @MainActor
    func testMailboxTimesOut() async {
        let box = Mailbox<Int>()
        do {
            _ = try await box.next(timeout: 0.1)
            XCTFail("Expected a timeout")
        } catch {
            XCTAssertEqual(error as? LinkError, .timeout)
        }
    }

    func testUUIDsMatchProtocol() {
        XCTAssertEqual(DeviceManager.serviceUUID.uuidString, "4F500001-6D69-6E69-8000-6F6E65706167")
        XCTAssertEqual(DeviceManager.transferDataUUID.uuidString, "4F50000A-6D69-6E69-8000-6F6E65706167")
    }

    func testErrorMessagesSayWhatToDo() {
        for code in DeviceErrorCode.allCases where code != .none {
            XCTAssertTrue(code.message.contains("."), "\(code) needs a full sentence")
        }
        XCTAssertFalse(UserFacingError("x", LinkError.timeout).message.isEmpty)
    }
}

import Foundation
import XCTest
@testable import OnePageKit
#if canImport(CryptoKit)
import CryptoKit
#endif

/// Tests added in review pass 6: the update manifest made by
/// shared/tools/make_manifest.py, damaged replies from the device, and the
/// transfer plan when the device discards a partial book.
final class CoverageTests: XCTestCase {
    struct ManifestFixture: Decodable {
        let public_key: String
        let image_hex: String
        let manifest: FirmwareManifest
    }

    func loadManifestFixture() throws -> (ManifestFixture, Data) {
        let url = LogicTests.fixtures.appendingPathComponent("manifest-test.json")
        let data = try Data(contentsOf: url)
        return (try JSONDecoder().decode(ManifestFixture.self, from: data), data)
    }

    static func hexBytes(_ s: String) -> [UInt8] {
        var out: [UInt8] = []
        var i = s.startIndex
        while i < s.endIndex {
            let j = s.index(i, offsetBy: 2)
            out.append(UInt8(s[i..<j], radix: 16)!)
            i = j
        }
        return out
    }

    func testManifestDecodeAndHardware() throws {
        let (fx, _) = try loadManifestFixture()
        let m = fx.manifest
        XCTAssertEqual(m.version, "1.0.1")
        XCTAssertEqual(m.size, Self.hexBytes(fx.image_hex).count)
        XCTAssertEqual(m.hw, [1])
        XCTAssertTrue(m.supports(hardwareRevision: 1))
        XCTAssertFalse(m.supports(hardwareRevision: 2))
        XCTAssertTrue(m.supports(hardwareRevision: nil))
        // The unknown "min_app" key is ignored; missing optional keys are fine.
        let minimal = #"{"version":"1.0.2","url":"https://example.invalid/a.bin","size":1,"sha256":"AB","signature":"x"}"#
        let m2 = try FirmwareManifest.decode(Data(minimal.utf8))
        XCTAssertNil(m2.hw)
        XCTAssertTrue(m2.supports(hardwareRevision: 7))
        // The signed text uses lower-case hex.
        XCTAssertEqual(String(decoding: m2.signedMessage, as: UTF8.self), "1.0.2\n1\nab")
        XCTAssertThrowsError(try FirmwareManifest.decode(Data(#"{"version":"1"}"#.utf8)))
    }

    #if canImport(CryptoKit)
    func testManifestSignatureFromReleaseTool() throws {
        let (fx, _) = try loadManifestFixture()
        let key = try Curve25519.Signing.PublicKey(rawRepresentation: XCTUnwrap(Data(base64Encoded: fx.public_key)))
        let sig = try XCTUnwrap(Data(base64Encoded: fx.manifest.signature))
        XCTAssertTrue(key.isValidSignature(sig, for: Data(fx.manifest.signedMessage)))
        // Any change to the signed fields breaks the signature.
        var changed = fx.manifest
        changed.size += 1
        XCTAssertFalse(key.isValidSignature(sig, for: Data(changed.signedMessage)))
        // The image hash in the manifest matches the image.
        let hash = SHA256.hash(data: Data(Self.hexBytes(fx.image_hex))).map { String(format: "%02x", $0) }.joined()
        XCTAssertEqual(hash, fx.manifest.sha256.lowercased())
    }
    #endif

    func testDamagedRepliesThrow() {
        // Truncated PROGRESS.
        XCTAssertThrowsError(try TransferReply(bytes: [1, 0x81, 1, 2, 3]))
        // Unknown transfer status.
        XCTAssertThrowsError(try TransferReply(bytes: [1, 0x86, 1, 0, 0, 0, 9]))
        // Wrong protocol version.
        XCTAssertThrowsError(try TransferReply(bytes: [2, 0x86, 1, 0, 0, 0, 0])) { error in
            XCTAssertEqual(error as? ProtocolError, .unsupportedVersion(2))
        }
        // LIST that claims one book but carries none.
        XCTAssertThrowsError(try TransferReply(bytes: [1, 0x85, 1, 0, 0, 0, 1]))
        // App filter LIST whose entry runs past the end.
        XCTAssertThrowsError(try AppFilterReply(bytes: [1, 0x81, 1, 0, 0, 0, 1, 0, 0, 0, 10, 0x61]))
        XCTAssertThrowsError(try AppFilterReply(bytes: [1, 0x99]))
        XCTAssertThrowsError(try AppFilterReply(bytes: []))
    }

    func testPlanFollowsDeviceWhenPartialBookIsDiscarded() {
        let file = [UInt8](repeating: 7, count: 500)
        var plan = TransferPlan(bookID: 9, file: file, maxWriteLength: 107)
        while plan.nextBlock() != nil {}
        plan.apply(deviceOffset: 400)
        XCTAssertEqual(plan.confirmedOffset, 400)
        // The device restarted the book from zero: progress follows it.
        XCTAssertTrue(plan.apply(deviceOffset: 0))
        XCTAssertEqual(plan.confirmedOffset, 0)
        XCTAssertEqual(plan.nextOffset, 0)
        // An offset past the end is clamped.
        plan.apply(deviceOffset: 9999)
        XCTAssertEqual(plan.confirmedOffset, 500)
        // A tiny link still makes progress, one byte per block.
        var tiny = TransferPlan(bookID: 1, file: [1, 2, 3], maxWriteLength: 1)
        var n = 0
        while tiny.nextBlock() != nil { n += 1 }
        XCTAssertEqual(n, 3)
    }

    func testCommandEncodings() {
        XCTAssertEqual(TransferCommand.fast(true).encode(), [1, 0x07, 1])
        XCTAssertEqual(TransferCommand.fast(false).encode(), [1, 0x07, 0])
        XCTAssertEqual(TransferCommand.delete(bookID: 0x01020304).encode(), [1, 0x04, 4, 3, 2, 1])
        XCTAssertEqual(TransferCommand.list(start: 0x0102).encode(), [1, 0x05, 2, 1])
    }
}

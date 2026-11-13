import Foundation

/// The firmware version file the app checks once a day.
///
/// ```json
/// { "version": "1.0.1", "url": "https://…/onepage-mini-1.0.1.bin",
///   "size": 312344, "sha256": "<64 hex>", "signature": "<base64 Ed25519>",
///   "min_app": "1.0", "hw": [1], "notes": "Fixes …" }
/// ```
///
/// `signature` is an Ed25519 signature over the UTF-8 bytes of
/// `"<version>\n<size>\n<sha256 lower-case hex>"`. The app checks it with
/// the public key built into the app, then checks the size and SHA-256 of the
/// downloaded file. MCUboot checks its own image signature as well.
public struct FirmwareManifest: Codable, Equatable {
    public var version: String
    public var url: URL
    public var size: Int
    public var sha256: String
    public var signature: String
    public var notes: String?
    public var hw: [Int]?

    public init(version: String, url: URL, size: Int, sha256: String, signature: String, notes: String? = nil, hw: [Int]? = nil) {
        self.version = version
        self.url = url
        self.size = size
        self.sha256 = sha256
        self.signature = signature
        self.notes = notes
        self.hw = hw
    }

    /// The bytes the signature covers.
    public var signedMessage: [UInt8] {
        Array("\(version)\n\(size)\n\(sha256.lowercased())".utf8)
    }

    public func supports(hardwareRevision: UInt8?) -> Bool {
        guard let hw, let rev = hardwareRevision else { return true }
        return hw.contains(Int(rev))
    }

    public static func decode(_ data: Data) throws -> FirmwareManifest {
        try JSONDecoder().decode(FirmwareManifest.self, from: data)
    }
}

/// Compares dotted version strings: "1.10.0" > "1.9.3". Text after "-" or "+" is ignored.
public enum SemVer {
    public static func compare(_ a: String, _ b: String) -> ComparisonResult {
        func parts(_ s: String) -> [Int] {
            let core = s.split(whereSeparator: { $0 == "-" || $0 == "+" }).first.map(String.init) ?? s
            return core.split(separator: ".").map { Int($0) ?? 0 }
        }
        let x = parts(a), y = parts(b)
        for i in 0..<max(x.count, y.count) {
            let l = i < x.count ? x[i] : 0
            let r = i < y.count ? y[i] : 0
            if l < r { return .orderedAscending }
            if l > r { return .orderedDescending }
        }
        return .orderedSame
    }

    public static func isNewer(_ candidate: String, than current: String) -> Bool {
        compare(candidate, current) == .orderedDescending
    }
}

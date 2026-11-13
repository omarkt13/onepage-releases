import Foundation

/// Advertising manufacturer data (protocol section 3):
/// `[company u16 = 0xFFFF][device_id 4 bytes][flags u8]`.
public struct AdvertInfo: Equatable {
    public static let companyID: UInt16 = 0xFFFF

    public var deviceID: [UInt8]
    public var pairing: Bool
    public var hasBond: Bool

    public init?(manufacturerData: [UInt8]) {
        guard manufacturerData.count >= 7 else { return nil }
        let company = UInt16(manufacturerData[0]) | UInt16(manufacturerData[1]) << 8
        guard company == Self.companyID else { return nil }
        deviceID = Array(manufacturerData[2..<6])
        let flags = manufacturerData[6]
        pairing = flags & 0x01 != 0
        hasBond = flags & 0x02 != 0
    }

    /// 8 upper-case hex characters: the 4 bytes read as a little-endian u32.
    public var deviceIDString: String { DeviceID.string(deviceID) }
}

public enum DeviceID {
    /// The 4 bytes as a little-endian u32, printed as `%08X`.
    public static func string(_ bytes: [UInt8]) -> String {
        guard bytes.count == 4 else { return bytes.map { String(format: "%02X", $0) }.joined() }
        let v = UInt32(bytes[0]) | UInt32(bytes[1]) << 8 | UInt32(bytes[2]) << 16 | UInt32(bytes[3]) << 24
        return String(format: "%08X", v)
    }

    /// True if an identifier from a link matches the advertised bytes. The
    /// protocol does not fix the print order, so both orders are accepted.
    public static func matches(_ text: String, _ bytes: [UInt8]) -> Bool {
        let t = text.uppercased()
        let forward = bytes.map { String(format: "%02X", $0) }.joined()
        return t == string(bytes) || t == forward
    }
}

/// The NFC tag's universal link: `https://onepage.app/p/<device_id>`.
public enum PairingLink {
    /// Placeholder until the domain is registered. Keep in step with the
    /// associated-domains entitlement and the firmware's NDEF record.
    public static let host = "onepage.app"

    public static func deviceID(from url: URL) -> String? {
        guard let comps = URLComponents(url: url, resolvingAgainstBaseURL: false),
              comps.scheme?.lowercased() == "https",
              comps.host?.lowercased() == host else { return nil }
        let parts = comps.path.split(separator: "/").map(String.init)
        guard parts.count == 2, parts[0] == "p" else { return nil }
        let id = parts[1].uppercased()
        guard id.count == 8, id.allSatisfy({ $0.isHexDigit }) else { return nil }
        return id
    }

    public static func url(for deviceID: String) -> URL? {
        URL(string: "https://\(host)/p/\(deviceID.uppercased())")
    }
}

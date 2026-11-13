import Foundation

/// Converts text to Windows-1252, the book character set (protocol section 6).
/// The rules match `to_cp1252` in shared/tools/opproto.py exactly.
public enum CP1252 {
    /// Unicode scalars that Windows-1252 places in 0x80–0x9F.
    static let high: [UInt32: UInt8] = [
        0x20AC: 0x80, 0x201A: 0x82, 0x0192: 0x83, 0x201E: 0x84, 0x2026: 0x85,
        0x2020: 0x86, 0x2021: 0x87, 0x02C6: 0x88, 0x2030: 0x89, 0x0160: 0x8A,
        0x2039: 0x8B, 0x0152: 0x8C, 0x017D: 0x8E, 0x2018: 0x91, 0x2019: 0x92,
        0x201C: 0x93, 0x201D: 0x94, 0x2022: 0x95, 0x2013: 0x96, 0x2014: 0x97,
        0x02DC: 0x98, 0x2122: 0x99, 0x0161: 0x9A, 0x203A: 0x9B, 0x0153: 0x9C,
        0x017E: 0x9E, 0x0178: 0x9F,
    ]

    /// Characters outside Windows-1252 with a close match. Empty means drop.
    static let fallback: [UInt32: String] = [
        0x2010: "-", 0x2011: "-", 0x2012: "-", 0x2015: "-", 0x2212: "-",
        0x00A0: " ", 0x2007: " ", 0x2009: " ", 0x200A: " ", 0x202F: " ",
        0x2002: " ", 0x2003: " ", 0x2032: "'", 0x2033: "\"",
        0x200B: "", 0xFEFF: "", 0x00AD: "",
    ]

    public static func encode(_ text: String) -> [UInt8] {
        var out: [UInt8] = []
        out.reserveCapacity(text.utf8.count)
        for s in text.unicodeScalars {
            let v = s.value
            if v == 0x0D { continue }
            if v == 0x0A { out.append(0x0A); continue }
            if v == 0x09 { out.append(0x20); continue }
            if v < 0x20 { continue }
            if let f = fallback[v] {
                for fs in f.unicodeScalars { out.append(single(fs.value)) }
                continue
            }
            out.append(single(v))
        }
        return out
    }

    static func single(_ v: UInt32) -> UInt8 {
        if v < 0x80 { return UInt8(v) }
        if v >= 0xA0 && v <= 0xFF { return UInt8(v) }
        if let b = high[v] { return b }
        return 0x3F // "?"
    }

    /// Decodes Windows-1252 bytes, for reading plain-text files that are not UTF-8.
    public static func decode(_ bytes: [UInt8]) -> String {
        var reverse: [UInt8: UInt32] = [:]
        for (k, v) in high { reverse[v] = k }
        var s = String.UnicodeScalarView()
        for b in bytes {
            let v: UInt32 = (b >= 0x80 && b < 0xA0) ? (reverse[b] ?? 0xFFFD) : UInt32(b)
            if let u = Unicode.Scalar(v) { s.append(u) }
        }
        return String(s)
    }
}

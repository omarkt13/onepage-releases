import Foundation

/// A forgiving tokenizer for the XML and XHTML found in EPUB files. It does not
/// validate. It is enough to read container.xml, the OPF file and chapter text.
public enum XMLToken: Equatable {
    case start(name: String, attributes: [String: String], selfClosing: Bool)
    case end(name: String)
    case text(String)
}

public enum MiniXML {
    public static func tokenize(_ source: String) -> [XMLToken] {
        var tokens: [XMLToken] = []
        let s = Array(source.unicodeScalars)
        var i = 0
        var text = String.UnicodeScalarView()

        func flushText() {
            if !text.isEmpty {
                tokens.append(.text(decodeEntities(String(text))))
                text = String.UnicodeScalarView()
            }
        }

        func hasPrefix(_ p: String, at k: Int) -> Bool {
            let ps = Array(p.unicodeScalars)
            guard k + ps.count <= s.count else { return false }
            for j in 0..<ps.count where s[k + j] != ps[j] { return false }
            return true
        }

        func skip(past end: String, from k: Int) -> Int {
            var j = k
            while j < s.count {
                if hasPrefix(end, at: j) { return j + end.unicodeScalars.count }
                j += 1
            }
            return s.count
        }

        while i < s.count {
            let c = s[i]
            if c != "<" {
                text.append(c)
                i += 1
                continue
            }
            if hasPrefix("<!--", at: i) { flushText(); i = skip(past: "-->", from: i + 4); continue }
            if hasPrefix("<![CDATA[", at: i) {
                let end = skip(past: "]]>", from: i + 9)
                let inner = s[(i + 9)..<max(i + 9, end - 3)]
                text.append(contentsOf: inner)
                i = end
                continue
            }
            if hasPrefix("<?", at: i) { flushText(); i = skip(past: "?>", from: i + 2); continue }
            if hasPrefix("<!", at: i) { flushText(); i = skip(past: ">", from: i + 2); continue }

            // A tag: read to the closing ">" outside quotes.
            var j = i + 1
            var quote: Unicode.Scalar?
            while j < s.count {
                let d = s[j]
                if let q = quote {
                    if d == q { quote = nil }
                } else if d == "\"" || d == "'" {
                    quote = d
                } else if d == ">" {
                    break
                }
                j += 1
            }
            var inner = String.UnicodeScalarView()
            inner.append(contentsOf: s[(i + 1)..<min(j, s.count)])
            i = j + 1
            flushText()
            tokens.append(parseTag(String(inner)))
        }
        flushText()
        return tokens
    }

    static func parseTag(_ raw: String) -> XMLToken {
        var body = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if body.hasPrefix("/") {
            return .end(name: localName(String(body.dropFirst()).trimmingCharacters(in: .whitespaces)))
        }
        var selfClosing = false
        if body.hasSuffix("/") {
            selfClosing = true
            body.removeLast()
        }
        let scalars = Array(body.unicodeScalars)
        var k = 0
        var name = String.UnicodeScalarView()
        while k < scalars.count, !isSpace(scalars[k]) { name.append(scalars[k]); k += 1 }
        var attrs: [String: String] = [:]
        while k < scalars.count {
            while k < scalars.count, isSpace(scalars[k]) { k += 1 }
            var key = String.UnicodeScalarView()
            while k < scalars.count, scalars[k] != "=", !isSpace(scalars[k]) { key.append(scalars[k]); k += 1 }
            while k < scalars.count, isSpace(scalars[k]) { k += 1 }
            var value = String.UnicodeScalarView()
            if k < scalars.count, scalars[k] == "=" {
                k += 1
                while k < scalars.count, isSpace(scalars[k]) { k += 1 }
                if k < scalars.count, scalars[k] == "\"" || scalars[k] == "'" {
                    let q = scalars[k]
                    k += 1
                    while k < scalars.count, scalars[k] != q { value.append(scalars[k]); k += 1 }
                    k += 1
                } else {
                    while k < scalars.count, !isSpace(scalars[k]) { value.append(scalars[k]); k += 1 }
                }
            }
            if !key.isEmpty { attrs[localName(String(key))] = decodeEntities(String(value)) }
        }
        return .start(name: localName(String(name)), attributes: attrs, selfClosing: selfClosing)
    }

    static func isSpace(_ c: Unicode.Scalar) -> Bool { c == " " || c == "\n" || c == "\t" || c == "\r" }

    /// Drops a namespace prefix and lower-cases: "dc:Title" → "title".
    static func localName(_ n: String) -> String {
        let base = n.split(separator: ":").last.map(String.init) ?? n
        return base.lowercased()
    }

    static let named: [String: String] = [
        "amp": "&", "lt": "<", "gt": ">", "quot": "\"", "apos": "'", "nbsp": "\u{00A0}",
        "mdash": "\u{2014}", "ndash": "\u{2013}", "hellip": "\u{2026}", "lsquo": "\u{2018}",
        "rsquo": "\u{2019}", "ldquo": "\u{201C}", "rdquo": "\u{201D}", "laquo": "\u{00AB}",
        "raquo": "\u{00BB}", "copy": "\u{00A9}", "reg": "\u{00AE}", "trade": "\u{2122}",
        "shy": "\u{00AD}", "eacute": "é", "egrave": "è", "aacute": "á", "agrave": "à",
        "uuml": "ü", "ouml": "ö", "auml": "ä", "szlig": "ß", "ccedil": "ç", "iacute": "í",
        "oacute": "ó", "uacute": "ú", "ntilde": "ñ", "middot": "·", "bull": "•", "deg": "°",
    ]

    public static func decodeEntities(_ s: String) -> String {
        guard s.contains("&") else { return s }
        var out = ""
        var i = s.startIndex
        while i < s.endIndex {
            let c = s[i]
            if c == "&", let semi = s[i...].firstIndex(of: ";"), s.distance(from: i, to: semi) <= 10 {
                let ent = String(s[s.index(after: i)..<semi])
                if let v = decodeEntity(ent) {
                    out += v
                    i = s.index(after: semi)
                    continue
                }
            }
            out.append(c)
            i = s.index(after: i)
        }
        return out
    }

    static func decodeEntity(_ e: String) -> String? {
        if e.hasPrefix("#x") || e.hasPrefix("#X") {
            return UInt32(e.dropFirst(2), radix: 16).flatMap(Unicode.Scalar.init).map { String(Character($0)) }
        }
        if e.hasPrefix("#") {
            return UInt32(e.dropFirst()).flatMap(Unicode.Scalar.init).map { String(Character($0)) }
        }
        return named[e]
    }
}

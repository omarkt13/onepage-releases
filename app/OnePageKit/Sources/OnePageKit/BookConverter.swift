import Foundation

public enum BookConversionError: Error, CustomStringConvertible {
    case unsupportedFormat(String)
    case drmProtected
    case empty
    case zip(ZipError)

    public var description: String {
        switch self {
        case .unsupportedFormat(let ext):
            return "The app cannot read .\(ext) files. Use an EPUB or a plain-text (.txt) file."
        case .drmProtected:
            return "This book has copy protection. Books bought from Apple Books or Kindle cannot be sent. Use a DRM-free EPUB."
        case .empty:
            return "The book has no text the device can show. Check that the file is not empty."
        case .zip(let e):
            return e.description
        }
    }
}

/// Converts EPUB and plain-text files to the device's OPB format, on the phone.
public enum BookConverter {
    public static func convert(fileName: String, bytes: [UInt8]) throws -> OPBBook {
        let ext = (fileName as NSString).pathExtension.lowercased()
        let fallbackTitle = ((fileName as NSString).lastPathComponent as NSString).deletingPathExtension
        switch ext {
        case "epub":
            do {
                return try convertEPUB(bytes, fallbackTitle: fallbackTitle)
            } catch let e as ZipError {
                if e == .encrypted { throw BookConversionError.drmProtected }
                throw BookConversionError.zip(e)
            }
        case "txt", "text", "md", "":
            return try convertText(bytes, title: fallbackTitle)
        default:
            throw BookConversionError.unsupportedFormat(ext)
        }
    }

    // MARK: Plain text

    public static func convertText(_ bytes: [UInt8], title: String) throws -> OPBBook {
        var body = bytes
        if body.starts(with: [0xEF, 0xBB, 0xBF]) { body.removeFirst(3) }
        let text = String(bytes: body, encoding: .utf8) ?? CP1252.decode(body)
        let lines = text.replacingOccurrences(of: "\r\n", with: "\n")
            .replacingOccurrences(of: "\r", with: "\n")
            .components(separatedBy: "\n")

        // Paragraphs are separated by blank lines. If the file has no blank
        // lines, each line is a paragraph.
        let hasBlank = lines.contains { $0.trimmingCharacters(in: .whitespaces).isEmpty }
        var paragraphs: [String] = []
        var current: [String] = []
        for line in lines {
            let t = line.trimmingCharacters(in: .whitespaces)
            if t.isEmpty {
                if !current.isEmpty { paragraphs.append(current.joined(separator: " ")); current = [] }
            } else if !hasBlank {
                paragraphs.append(t)
            } else if isChapterHeading(t) {
                if !current.isEmpty { paragraphs.append(current.joined(separator: " ")); current = [] }
                paragraphs.append(t)
            } else {
                current.append(t)
            }
        }
        if !current.isEmpty { paragraphs.append(current.joined(separator: " ")) }
        guard !paragraphs.isEmpty else { throw BookConversionError.empty }

        var out: [UInt8] = []
        var chapters: [OPBBook.Chapter] = []
        for p in paragraphs {
            if isChapterHeading(p) {
                chapters.append(.init(textOffset: UInt32(out.count), name: p))
            }
            out += CP1252.encode(p)
            out.append(0x0A)
        }
        if chapters.isEmpty || chapters[0].textOffset != 0 {
            chapters.insert(.init(textOffset: 0, name: "Start"), at: 0)
        }
        return OPBBook(title: title, author: "", chapters: chapters, text: out)
    }

    static func isChapterHeading(_ line: String) -> Bool {
        guard line.count <= 60 else { return false }
        let words = line.split(separator: " ")
        guard let first = words.first, words.count <= 8 else { return false }
        let heads = ["chapter", "book", "part", "prologue", "epilogue", "act"]
        let f = first.lowercased().trimmingCharacters(in: .punctuationCharacters)
        guard heads.contains(f) else { return false }
        return f == "prologue" || f == "epilogue" || words.count >= 2
    }

    // MARK: EPUB

    public static func convertEPUB(_ bytes: [UInt8], fallbackTitle: String) throws -> OPBBook {
        let zip = try ZipArchive(bytes: bytes)
        if zip.contains("META-INF/encryption.xml") {
            // Font obfuscation alone is allowed; any other encryption is DRM.
            let enc = (try? zip.readString("META-INF/encryption.xml")) ?? ""
            let methods = MiniXML.tokenize(enc).compactMap { t -> String? in
                if case let .start(name, attrs, _) = t, name == "encryptionmethod" { return attrs["algorithm"] }
                return nil
            }
            let fontOnly = !methods.isEmpty && methods.allSatisfy {
                $0.contains("idpf.org/2008/embedding") || $0.contains("ns.adobe.com/pdf/enc#RC")
            }
            if !fontOnly { throw BookConversionError.drmProtected }
        }
        if zip.contains("META-INF/rights.xml") { throw BookConversionError.drmProtected }

        let container = try zip.readString("META-INF/container.xml")
        guard let opfPath = MiniXML.tokenize(container).lazy.compactMap({ t -> String? in
            if case let .start(name, attrs, _) = t, name == "rootfile" { return attrs["full-path"] }
            return nil
        }).first else { throw ZipError.missing("package file (OPF)") }

        let opf = MiniXML.tokenize(try zip.readString(opfPath))
        let baseDir = (opfPath as NSString).deletingLastPathComponent

        var manifest: [String: (href: String, type: String)] = [:]
        var spine: [String] = []
        var title = "", author = ""
        var capture: String?
        for t in opf {
            switch t {
            case let .start(name, attrs, selfClosing):
                if name == "item", let id = attrs["id"], let href = attrs["href"] {
                    manifest[id] = (href, attrs["media-type"] ?? "")
                } else if name == "itemref", let idref = attrs["idref"], attrs["linear"] != "no" {
                    spine.append(idref)
                } else if !selfClosing, name == "title" || name == "creator" {
                    capture = name
                }
            case let .text(s):
                if capture == "title", title.isEmpty { title = s.trimmingCharacters(in: .whitespacesAndNewlines) }
                if capture == "creator", author.isEmpty { author = s.trimmingCharacters(in: .whitespacesAndNewlines) }
            case .end:
                capture = nil
            }
        }

        var text: [UInt8] = []
        var chapters: [OPBBook.Chapter] = []
        for (index, idref) in spine.enumerated() {
            guard let item = manifest[idref], item.type.contains("html") else { continue }
            let path = resolve(base: baseDir, href: item.href)
            guard let xhtml = try? zip.readString(path) else { continue }
            let (paragraphs, heading) = XHTMLText.extract(xhtml)
            guard !paragraphs.isEmpty else { continue }
            let name = heading ?? "Section \(index + 1)"
            chapters.append(.init(textOffset: UInt32(text.count), name: name))
            for p in paragraphs {
                let encoded = CP1252.encode(p)
                if encoded.isEmpty { continue }
                text += encoded
                text.append(0x0A)
            }
        }
        guard !text.isEmpty else { throw BookConversionError.empty }
        return OPBBook(title: title.isEmpty ? fallbackTitle : title, author: author, chapters: chapters, text: text)
    }

    /// Resolves an href relative to the OPF directory, with "." and "..".
    static func resolve(base: String, href: String) -> String {
        let clean = (href.removingPercentEncoding ?? href).split(separator: "#").first.map(String.init) ?? href
        var parts = base.isEmpty ? [] : base.split(separator: "/").map(String.init)
        for p in clean.split(separator: "/").map(String.init) {
            if p == ".." { if !parts.isEmpty { parts.removeLast() } } else if p != "." { parts.append(p) }
        }
        return parts.joined(separator: "/")
    }
}

/// XHTML chapter → paragraphs of plain text, plus the first heading.
public enum XHTMLText {
    static let blockTags: Set<String> = [
        "p", "div", "br", "li", "h1", "h2", "h3", "h4", "h5", "h6", "blockquote", "tr",
        "section", "article", "pre", "hr", "dt", "dd", "figcaption", "table", "ul", "ol", "body",
    ]
    static let skipTags: Set<String> = ["head", "script", "style", "svg", "math", "title"]
    static let headingTags: Set<String> = ["h1", "h2", "h3"]

    public static func extract(_ xhtml: String) -> (paragraphs: [String], heading: String?) {
        var paragraphs: [String] = []
        var current = ""
        var skipDepth = 0
        var heading: String?
        var inHeading = false
        var headingText = ""

        func endParagraph() {
            let collapsed = current.split(whereSeparator: { $0 == " " || $0 == "\n" || $0 == "\t" || $0 == "\r" })
                .joined(separator: " ")
            if !collapsed.isEmpty { paragraphs.append(collapsed) }
            current = ""
        }

        for t in MiniXML.tokenize(xhtml) {
            switch t {
            case let .start(name, _, selfClosing):
                if skipTags.contains(name) { if !selfClosing { skipDepth += 1 }; continue }
                if blockTags.contains(name) { endParagraph() }
                if headingTags.contains(name), !selfClosing, heading == nil { inHeading = true; headingText = "" }
                if name == "img" || name == "image" { continue } // Images are dropped in version 1.
            case let .end(name):
                if skipTags.contains(name) { skipDepth = max(0, skipDepth - 1); continue }
                if blockTags.contains(name) { endParagraph() }
                if headingTags.contains(name), inHeading {
                    inHeading = false
                    let h = headingText.split(whereSeparator: { $0.isWhitespace }).joined(separator: " ")
                    if !h.isEmpty { heading = h }
                }
            case let .text(s):
                guard skipDepth == 0 else { continue }
                current += s
                if inHeading { headingText += s }
            }
        }
        endParagraph()
        return (paragraphs, heading)
    }
}

import Foundation
#if canImport(Compression)
import Compression
#endif

public enum InflateError: Error, Equatable {
    case invalidData(String)
    case outputTooLarge
}

/// Raw DEFLATE (RFC 1951) decoding. Uses Apple's Compression framework when
/// it exists, and the pure-Swift decoder otherwise or if the framework fails.
public enum Inflate {
    public static func inflate(_ input: [UInt8], expectedSize: Int, limit: Int = 64 << 20) throws -> [UInt8] {
        guard expectedSize <= limit else { throw InflateError.outputTooLarge }
        #if canImport(Compression)
        if expectedSize > 0, let out = systemInflate(input, expectedSize: expectedSize) {
            return out
        }
        #endif
        return try PureInflate.decode(input, limit: limit)
    }

    #if canImport(Compression)
    static func systemInflate(_ input: [UInt8], expectedSize: Int) -> [UInt8]? {
        guard !input.isEmpty else { return nil }
        var out = [UInt8](repeating: 0, count: expectedSize)
        let n = out.withUnsafeMutableBufferPointer { dst in
            input.withUnsafeBufferPointer { src in
                compression_decode_buffer(dst.baseAddress!, expectedSize, src.baseAddress!, src.count, nil, COMPRESSION_ZLIB)
            }
        }
        return n == expectedSize ? out : nil
    }
    #endif
}

/// A small, plain DEFLATE decoder after Mark Adler's `puff.c`.
public enum PureInflate {
    struct Huffman {
        var count = [Int](repeating: 0, count: 16)
        var symbol: [Int]

        init(lengths: [Int]) throws {
            symbol = [Int](repeating: 0, count: lengths.count)
            for l in lengths { count[l] += 1 }
            if count[0] == lengths.count { return } // No codes: allowed, fails on use.
            var left = 1
            for len in 1..<16 {
                left <<= 1
                left -= count[len]
                if left < 0 { throw InflateError.invalidData("over-subscribed code") }
            }
            var offs = [Int](repeating: 0, count: 16)
            for len in 1..<15 { offs[len + 1] = offs[len] + count[len] }
            for (sym, l) in lengths.enumerated() where l != 0 {
                symbol[offs[l]] = sym
                offs[l] += 1
            }
        }
    }

    struct State {
        let input: [UInt8]
        var pos = 0
        var bitBuf = 0
        var bitCnt = 0
        var out: [UInt8] = []
        let limit: Int

        mutating func bits(_ need: Int) throws -> Int {
            var val = bitBuf
            while bitCnt < need {
                guard pos < input.count else { throw InflateError.invalidData("input ended") }
                val |= Int(input[pos]) << bitCnt
                pos += 1
                bitCnt += 8
            }
            bitBuf = val >> need
            bitCnt -= need
            return val & ((1 << need) - 1)
        }

        mutating func decode(_ h: Huffman) throws -> Int {
            var code = 0, first = 0, index = 0
            for len in 1..<16 {
                code |= try bits(1)
                let count = h.count[len]
                if code - count < first { return h.symbol[index + (code - first)] }
                index += count
                first += count
                first <<= 1
                code <<= 1
            }
            throw InflateError.invalidData("bad code")
        }
    }

    static let lenBase = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                          35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258]
    static let lenExtra = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                           3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0]
    static let distBase = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                           257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                           8193, 12289, 16385, 24577]
    static let distExtra = [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                            7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13]

    static let fixed: (Huffman, Huffman) = {
        var l = [Int](repeating: 8, count: 288)
        for i in 144..<256 { l[i] = 9 }
        for i in 256..<280 { l[i] = 7 }
        let lit = try! Huffman(lengths: l)
        let dist = try! Huffman(lengths: [Int](repeating: 5, count: 30))
        return (lit, dist)
    }()

    public static func decode(_ input: [UInt8], limit: Int = 64 << 20) throws -> [UInt8] {
        var s = State(input: input, limit: limit)
        var last = 0
        repeat {
            last = try s.bits(1)
            let type = try s.bits(2)
            switch type {
            case 0: try stored(&s)
            case 1: try codes(&s, fixed.0, fixed.1)
            case 2:
                let (l, d) = try dynamic(&s)
                try codes(&s, l, d)
            default: throw InflateError.invalidData("block type 3")
            }
        } while last == 0
        return s.out
    }

    static func stored(_ s: inout State) throws {
        s.bitBuf = 0
        s.bitCnt = 0
        guard s.pos + 4 <= s.input.count else { throw InflateError.invalidData("stored header") }
        let len = Int(s.input[s.pos]) | Int(s.input[s.pos + 1]) << 8
        let nlen = Int(s.input[s.pos + 2]) | Int(s.input[s.pos + 3]) << 8
        s.pos += 4
        guard len == (~nlen & 0xFFFF) else { throw InflateError.invalidData("stored length") }
        guard s.pos + len <= s.input.count else { throw InflateError.invalidData("stored data") }
        guard s.out.count + len <= s.limit else { throw InflateError.outputTooLarge }
        s.out.append(contentsOf: s.input[s.pos..<(s.pos + len)])
        s.pos += len
    }

    static func codes(_ s: inout State, _ lencode: Huffman, _ distcode: Huffman) throws {
        while true {
            var sym = try s.decode(lencode)
            if sym < 256 {
                guard s.out.count < s.limit else { throw InflateError.outputTooLarge }
                s.out.append(UInt8(sym))
            } else if sym == 256 {
                return
            } else {
                sym -= 257
                guard sym < 29 else { throw InflateError.invalidData("length symbol") }
                let len = try lenBase[sym] + s.bits(lenExtra[sym])
                let dsym = try s.decode(distcode)
                guard dsym < 30 else { throw InflateError.invalidData("distance symbol") }
                let dist = try distBase[dsym] + s.bits(distExtra[dsym])
                guard dist <= s.out.count else { throw InflateError.invalidData("distance too far") }
                guard s.out.count + len <= s.limit else { throw InflateError.outputTooLarge }
                let start = s.out.count - dist
                for i in 0..<len { s.out.append(s.out[start + i]) }
            }
        }
    }

    static let order = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15]

    static func dynamic(_ s: inout State) throws -> (Huffman, Huffman) {
        let nlen = try s.bits(5) + 257
        let ndist = try s.bits(5) + 1
        let ncode = try s.bits(4) + 4
        guard nlen <= 286, ndist <= 30 else { throw InflateError.invalidData("bad counts") }
        var lengths = [Int](repeating: 0, count: 19)
        for i in 0..<ncode { lengths[order[i]] = try s.bits(3) }
        let lencode = try Huffman(lengths: lengths)
        var all = [Int](repeating: 0, count: nlen + ndist)
        var index = 0
        while index < nlen + ndist {
            var sym = try s.decode(lencode)
            if sym < 16 {
                all[index] = sym
                index += 1
            } else {
                var len = 0
                if sym == 16 {
                    guard index > 0 else { throw InflateError.invalidData("repeat with no length") }
                    len = all[index - 1]
                    sym = try 3 + s.bits(2)
                } else if sym == 17 {
                    sym = try 3 + s.bits(3)
                } else {
                    sym = try 11 + s.bits(7)
                }
                guard index + sym <= nlen + ndist else { throw InflateError.invalidData("too many lengths") }
                for _ in 0..<sym {
                    all[index] = len
                    index += 1
                }
            }
        }
        guard all[256] != 0 else { throw InflateError.invalidData("no end-of-block code") }
        let l = try Huffman(lengths: Array(all[0..<nlen]))
        let d = try Huffman(lengths: Array(all[nlen..<(nlen + ndist)]))
        return (l, d)
    }
}

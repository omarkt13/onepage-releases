#!/usr/bin/env python3
"""Reference encoder for the OnePage Mini Bluetooth protocol (docs/protocol.md).

Run it to regenerate shared/test-vectors/vectors.json:

    python3 -I shared/tools/opproto.py shared/test-vectors/vectors.json

The firmware host tests and the app's Swift tests both read that file, so the
two sides cannot drift apart without a test failing.
"""
import json
import struct
import sys
import zlib

VERSION = 1
ELLIPSIS = "…"


# --- checksums -------------------------------------------------------------

def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


# --- TLV -------------------------------------------------------------------

def tlv(records):
    out = bytearray([VERSION])
    for tag, value in records:
        assert len(value) <= 255
        out += bytes([tag, len(value)]) + value
    return bytes(out)


def u8(v): return struct.pack("<B", v)
def u16(v): return struct.pack("<H", v)
def i16(v): return struct.pack("<h", v)
def u32(v): return struct.pack("<I", v)


# --- text rules ------------------------------------------------------------

def truncate(text: str, max_chars: int, max_bytes: int) -> str:
    """Cut to max_chars scalar values and max_bytes UTF-8 bytes, with an ellipsis."""
    if len(text) <= max_chars and len(text.encode()) <= max_bytes:
        return text
    keep = max_chars - 1
    out = text[:keep]
    while len((out + ELLIPSIS).encode()) > max_bytes:
        out = out[:-1]
    return out + ELLIPSIS


def lstr(s: str) -> bytes:
    b = s.encode()
    assert len(b) <= 255
    return bytes([len(b)]) + b


# --- messages --------------------------------------------------------------

def app_filter_entry(mode, pattern, flags, ident):
    b = ident.encode()
    return bytes([mode, pattern, flags, len(b)]) + b


def calendar_blob(generated_at, events):
    out = bytearray(u32(generated_at) + u16(len(events)))
    for e in events:
        title = truncate(e["title"], 80, 160)
        place = truncate(e.get("place", ""), 60, 120)
        out += u32(e["start"]) + u32(e["end"]) + u8(1 if e.get("all_day") else 0)
        out += lstr(title) + lstr(place)
    return bytes(out)


def transfer_block(offset, data):
    return bytes([VERSION]) + u32(offset) + u16(crc16(data)) + data


# Characters outside Windows-1252 that have a close match.
CP1252_FALLBACK = {
    "‐": "-", "‑": "-", "‒": "-", "―": "-",
    "−": "-", " ": " ", " ": " ", " ": " ", " ": " ",
    " ": " ", " ": " ", " ": " ", "′": "'", "″": '"',
    "​": "", "﻿": "", "­": "",
}


def to_cp1252(text: str) -> bytes:
    out = bytearray()
    for ch in text:
        if ch in ("\r",):
            continue
        if ch == "\n":
            out.append(0x0A)
            continue
        if ch == "\t":
            out.append(0x20)
            continue
        if ord(ch) < 0x20:
            continue
        ch = CP1252_FALLBACK.get(ch, ch)
        if ch == "":
            continue
        try:
            out += ch.encode("cp1252")
        except UnicodeEncodeError:
            out.append(ord("?"))
    return bytes(out)


def opb(title, author, chapters, text_cp1252: bytes):
    """chapters: list of (text_offset, name)."""
    meta = lstr(title) + lstr(author)
    table = b"".join(u32(off) + lstr(name) for off, name in chapters)
    text_off = 16 + len(meta) + len(table)
    header = b"OPB1" + u8(1) + u8(1) + u16(len(chapters)) + u32(text_off) + u32(len(text_cp1252))
    return header + meta + table + text_cp1252


# --- vectors ---------------------------------------------------------------

def hx(b: bytes) -> str:
    return b.hex()


def build_vectors():
    v = {"protocol_version": VERSION}

    v["crc"] = [
        {"input": hx(b"123456789"), "crc32": 0xCBF43926, "crc16": 0x29B1},
        {"input": "", "crc32": 0, "crc16": 0xFFFF},
        {"input": hx(bytes(range(256))), "crc32": crc32(bytes(range(256))),
         "crc16": crc16(bytes(range(256)))},
    ]

    v["status"] = {
        "fields": {"battery": 87, "charging": 1, "firmware": "1.0.0",
                   "storage_free_kib": 60000, "storage_total_kib": 65536,
                   "last_error": 0, "calendar_version": 42,
                   "calendar_synced_at": 847000000, "hw_rev": 1},
        "bytes": hx(tlv([(0x01, u8(87)), (0x02, u8(1)), (0x03, b"1.0.0"),
                         (0x04, u32(60000)), (0x05, u32(65536)), (0x06, u16(0)),
                         (0x07, u32(42)), (0x08, u32(847000000)), (0x09, u8(1))])),
    }

    v["settings"] = {
        "fields": {"quiet_on": 1, "quiet_start": 1320, "quiet_end": 420,
                   "refresh_mode": 1, "clock_12h": 0, "language": "en",
                   "alert_strength": 2},
        "bytes": hx(tlv([(0x01, u8(1)), (0x02, u16(1320)), (0x03, u16(420)),
                         (0x04, u8(1)), (0x05, u8(0)), (0x06, b"en"), (0x07, u8(2))])),
        # A partial write with one unknown tag (0x7F) the receiver must skip.
        "partial_with_unknown": hx(tlv([(0x7F, b"\x01\x02\x03"), (0x07, u8(3))])),
        "partial_expect": {"alert_strength": 3},
    }

    v["app_filter"] = {
        "entry": {"mode": 1, "pattern": 2, "flags": 1, "id": "com.example.bank"},
        "entry_bytes": hx(app_filter_entry(1, 2, 1, "com.example.bank")),
        "set_bytes": hx(bytes([VERSION, 0x02]) + app_filter_entry(1, 2, 1, "com.example.bank")),
        "list_request_bytes": hx(bytes([VERSION, 0x01]) + u16(0)),
        "list_reply": {
            "total": 2, "start": 0,
            "entries": [{"mode": 0, "pattern": 0, "flags": 0, "id": "com.apple.MobileSMS"},
                        {"mode": 2, "pattern": 4, "flags": 0, "id": "com.example.game"}],
            "bytes": hx(bytes([VERSION, 0x81]) + u16(2) + u16(0) + u8(2)
                        + app_filter_entry(0, 0, 0, "com.apple.MobileSMS")
                        + app_filter_entry(2, 4, 0, "com.example.game")),
        },
    }

    v["home_data"] = {
        "fields": {"temp_now": -35, "temp_high": 21, "temp_low": -80,
                   "condition": 8, "valid_until": 847003600, "place": "Zürich", "unit": 0},
        "bytes": hx(tlv([(0x01, i16(-35)), (0x02, i16(21)), (0x03, i16(-80)),
                         (0x04, u8(8)), (0x05, u32(847003600)),
                         (0x06, "Zürich".encode()), (0x07, u8(0))])),
    }

    long_title = "Quarterly planning review with the hardware, firmware and app teams, plus suppliers"
    emoji_title = "🎉" * 85
    events = [
        {"start": 847000000, "end": 847003600, "title": "Stand-up", "place": "Room 4"},
        {"start": 846979200, "end": 847065600, "all_day": True, "title": "Café day ☕", "place": ""},
        {"start": 847010000, "end": 847013600, "title": long_title,
         "place": "Building 7, third floor, the large meeting room next to the kitchen area"},
        {"start": 847020000, "end": 847023600, "title": emoji_title, "place": "x"},
    ]
    blob = calendar_blob(847000100, events)
    v["calendar"] = {
        "generated_at": 847000100,
        "events_in": events,
        "events_out": [
            {"start": e["start"], "end": e["end"], "all_day": bool(e.get("all_day")),
             "title": truncate(e["title"], 80, 160),
             "place": truncate(e.get("place", ""), 60, 120)} for e in events
        ],
        "blob": hx(blob),
        "crc32": crc32(blob),
        "begin_bytes": hx(bytes([VERSION, 0x01]) + u32(7) + u32(len(blob)) + u32(crc32(blob))),
        "data_first_bytes": hx(bytes([VERSION, 0x02]) + u32(0) + blob[:100]),
        "commit_bytes": hx(bytes([VERSION, 0x03]) + u32(7)),
        "ack_ok_bytes": hx(bytes([VERSION, 0x80, 0]) + u32(7) + u32(len(blob))),
    }

    v["truncate"] = [
        {"in": "short", "max_chars": 80, "max_bytes": 160, "out": "short"},
        {"in": long_title, "max_chars": 80, "max_bytes": 160, "out": truncate(long_title, 80, 160)},
        {"in": emoji_title, "max_chars": 80, "max_bytes": 160, "out": truncate(emoji_title, 80, 160)},
        {"in": "x" * 80, "max_chars": 80, "max_bytes": 160, "out": "x" * 80},
        {"in": "x" * 81, "max_chars": 80, "max_bytes": 160, "out": "x" * 79 + ELLIPSIS},
    ]

    v["data_request"] = [
        {"kind": 1, "reason": 0, "bytes": hx(bytes([VERSION, 1, 0]))},
        {"kind": 3, "reason": 1, "bytes": hx(bytes([VERSION, 3, 1]))},
    ]

    v["privacy"] = {
        "read_bytes": hx(tlv([(0x01, u8(1))])),
        "erase_bytes": hx(tlv([(0xE0, b"ERAS")])),
    }

    data = b"It was a bright cold day in April, and the clocks were striking thirteen.\n"
    v["transfer"] = {
        "start_bytes": hx(bytes([VERSION, 0x01]) + u32(0xA1B2C3D4) + u32(70000)
                          + u32(0x12345678) + lstr("1984")),
        "block": {"offset": 4096, "data": hx(data), "crc16": crc16(data),
                  "bytes": hx(transfer_block(4096, data))},
        "progress_bytes": hx(bytes([VERSION, 0x81]) + u32(0xA1B2C3D4) + u8(1) + u32(4096)),
        "list_reply_bytes": hx(bytes([VERSION, 0x85]) + u16(1) + u16(0) + u8(1)
                               + u32(0xA1B2C3D4) + u32(70000) + u32(35000) + lstr("1984")),
    }

    text = "Chapter One\nThe “quick” brown fox — naïve café… 1‑2 ✓\nChapter Two\nEnd.\n"
    cp = to_cp1252(text)
    ch2 = cp.index(b"Chapter Two")
    book = opb("Test Book", "A. Writer", [(0, "Chapter One"), (ch2, "Chapter Two")], cp)
    v["cp1252"] = {"in": text, "out": hx(cp)}
    v["opb"] = {"title": "Test Book", "author": "A. Writer",
                "chapters": [[0, "Chapter One"], [ch2, "Chapter Two"]],
                "bytes": hx(book), "crc32": crc32(book)}
    return v


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "shared/test-vectors/vectors.json"
    with open(out, "w", encoding="utf-8") as f:
        json.dump(build_vectors(), f, indent=2, ensure_ascii=False)
        f.write("\n")


if __name__ == "__main__":
    main()

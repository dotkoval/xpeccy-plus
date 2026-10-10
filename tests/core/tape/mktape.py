#!/usr/bin/env python3
"""Write the tape bench's images into a folder: test.tap, test.tzx, and for each
of them a .bytes file listing the bytes every data block carries, in order, which
the bench checks the decoded blocks against.

  mktape.py <folder>
"""

import struct
import sys
from pathlib import Path


def fnv(data):
    h = 2166136261
    for c in data:
        h = ((h ^ c) * 16777619) & 0xffffffff
    return h


def tap_body(flag, data):
    """A block as a loader sees it: flag, data, xor checksum."""
    b = bytes([flag]) + data
    x = 0
    for c in b:
        x ^= c
    return b + bytes([x])


def header(kind, name, length, par1, par2):
    return bytes([kind]) + name.ljust(10).encode("ascii") + struct.pack("<HHH", length, par1, par2)


def pattern(n, seed):
    return bytes((i * seed + (i >> 3)) & 0xff for i in range(n))


def write_tap(path, blocks):
    with open(path, "wb") as f:
        for b in blocks:
            f.write(struct.pack("<H", len(b)) + b)


def tzx_block(bid, payload):
    return bytes([bid]) + payload


def main():
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)

    code = pattern(256, 7)
    tap = [
        tap_body(0x00, header(3, "quicktest", len(code), 32768, 32768)),
        tap_body(0xff, code),
        tap_body(0xff, pattern(100, 13)),		# headerless
    ]
    write_tap(out / "test.tap", tap)

    std_hdr = tap_body(0x00, header(3, "tzxtest", 64, 40000, 32768))
    std_data = tap_body(0xff, pattern(64, 3))
    turbo = tap_body(0xff, pattern(64, 5))
    looped = tap_body(0xff, pattern(24, 9))
    last = tap_body(0xff, pattern(32, 17))
    blocks = [
        tzx_block(0x30, bytes([21]) + b"xpeccy-plus tape test"),
        tzx_block(0x10, struct.pack("<HH", 1000, len(std_hdr)) + std_hdr),
        tzx_block(0x10, struct.pack("<HH", 1000, len(std_data)) + std_data),
        # turbo: pilot, sync1, sync2, zero, one, pilot count, used bits, pause, length
        tzx_block(0x11, struct.pack("<HHHHHHBH", 2000, 600, 700, 500, 1000, 1500, 8, 500)
                  + struct.pack("<I", len(turbo))[:3] + turbo),
        # a pilot and syncs of its own for the pure data after them: one block, no bytes
        tzx_block(0x12, struct.pack("<HH", 1500, 200)),
        tzx_block(0x13, bytes([3]) + struct.pack("<HHH", 800, 900, 1000)),
        tzx_block(0x14, struct.pack("<HHBH", 600, 1200, 8, 1000) + struct.pack("<I", 16)[:3] + pattern(16, 11)),
        tzx_block(0x21, bytes([4]) + b"loop"),
        tzx_block(0x24, struct.pack("<H", 2)),
        tzx_block(0x10, struct.pack("<HH", 300, len(looped)) + looped),
        tzx_block(0x25, b""),
        tzx_block(0x22, b""),
        tzx_block(0x2a, struct.pack("<I", 0)),
        tzx_block(0x10, struct.pack("<HH", 1000, len(last)) + last),
    ]
    with open(out / "test.tzx", "wb") as f:
        f.write(b"ZXTape!\x1a" + bytes([1, 20]))
        for b in blocks:
            f.write(b)

    for name, datas in (("test.tap", tap), ("test.tzx", [std_hdr, std_data, turbo, looped, looped, last])):
        lines = [f"{len(d)} {fnv(d):08X}" for d in datas]
        (out / (name + ".bytes")).write_text("\n".join(lines) + "\n", encoding="ascii")
    return 0


if __name__ == "__main__":
    sys.exit(main())

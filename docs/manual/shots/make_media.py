"""The tape and disk the manual's screenshots show: python make_media.py <out folder>

Made here rather than taken from a real program, so a picture of the Tape Player or
the Disk Manager shows nobody's work. demo.tap holds a BASIC program, a screen and a
code block; demo.trd a TR-DOS disk with the same files and a label; demo.zip both tapes
of a two-sided "game" and its disk, as an archive the open dialog lists.
"""

import struct
import sys
import zipfile
from pathlib import Path


def tap_block(flag, data):
    body = bytes([flag]) + data
    chk = 0
    for b in body:
        chk ^= b
    body += bytes([chk])
    return struct.pack("<H", len(body)) + body


def header(kind, name, length, p1, p2):
    return bytes([kind]) + name.ljust(10).encode("ascii") + struct.pack("<HHH", length, p1, p2)


def basic():
    # 10 BORDER 0: PRINT "Xpeccy+": LOAD ""SCREEN$: LOAD ""CODE: RANDOMIZE USR 32768
    num = lambda v: str(v).encode() + b"\x0e\x00\x00" + struct.pack("<H", v) + b"\x00"
    line = (b"\xe7" + num(0) + b":\xf5\"Xpeccy+\":\xef\"\"\xaa:\xef\"\"\xaf:\xf9\xc0" + num(32768) + b"\x0d")
    return struct.pack(">H", 10) + struct.pack("<H", len(line)) + line


def screen():
    # a few stripes of ink, white paper and black ink
    pix = bytearray(6144)
    for i in range(0, 6144, 7):
        pix[i] = 0xAA
    return bytes(pix) + bytes([0x38] * 768)


def code():
    return bytes([0xF3, 0x3E, 0x02, 0xD3, 0xFE, 0x18, 0xFE]) + bytes(249)


FILES = [  # name, TR-DOS ext, tape type, data, param1 (start or autostart line)
    ("demo", "B", 0, basic(), 10),
    ("screen", "C", 3, screen(), 16384),
    ("code", "C", 3, code(), 32768),
]


def make_tap():
    out = b""
    for name, _, kind, data, p1 in FILES:
        p2 = len(data) if kind == 0 else 32768
        out += tap_block(0x00, header(kind, name, len(data), p1, p2))
        out += tap_block(0xFF, data)
    return out


def make_trd():
    disk = bytearray(80 * 2 * 16 * 256)
    track, sector = 1, 0
    for i, (name, ext, kind, data, p1) in enumerate(FILES):
        secs = (len(data) + 255) // 256
        entry = name.ljust(8).encode("ascii") + ext.encode("ascii")
        entry += struct.pack("<HH", p1 if ext != "B" else len(data), len(data))
        entry += bytes([secs, sector, track])
        disk[i * 16:i * 16 + 16] = entry
        at = (track * 16 + sector) * 256
        disk[at:at + len(data)] = data
        sector += secs
        track += sector // 16
        sector %= 16
    info = 8 * 256
    used = (track - 1) * 16 + sector
    disk[info + 0xE1] = sector
    disk[info + 0xE2] = track
    disk[info + 0xE3] = 0x16                    # 80 tracks, two sides
    disk[info + 0xE4] = len(FILES)
    disk[info + 0xE5:info + 0xE7] = struct.pack("<H", 159 * 16 - used)
    disk[info + 0xE7] = 0x10                    # TR-DOS
    disk[info + 0xF5:info + 0xFD] = b"MANUAL  "
    return bytes(disk)


# The breakpoint pictures: a loop that fills 256 bytes of the screen, timed by a
# span from START to DONE, with its labels and a list of breakpoints.
TIMING_BIN = bytes([
    0x21, 0x00, 0x40,   # START: ld hl,#4000
    0x01, 0x00, 0x01,   #        ld bc,#0100
    0x36, 0xFF,         # FILL:  ld (hl),#ff
    0x23,               #        inc hl
    0x0B,               #        dec bc
    0x78,               #        ld a,b
    0xB1,               #        or c
    0x20, 0xF8,         #        jr nz,FILL
    0x18, 0xF0,         # DONE:  jr START
])

TIMING_LAB = ":8000 START\n:8006 FILL\n:800E DONE\n"

# Exolon (the shots download it): its ammo and grenade counters, and the first frame of
# its hero running, 3 bytes by 32 lines a frame
EXOLON_LAB = ":8E5A AMMO\n:8E66 GRENADES\n:EF84 VITORC\n"
EXOLON_XBRK = """; Xpeccy+ Debugger breakpoints list
CPU:AMMO::W:DBG:
CPU:4000-57FF::W:CNT:
IO:00FE:0001:W:CNT:
"""

# The Cheats picture: Exolon's two counters (EXOLON_LAB), each a trainer that asks
# for its value, as a cheat file has them
EXOLON_POK = ("NAmmo\r\nZ  8 36442 256   0\r\n"
    "NGrenades\r\nZ  8 36454 256   0\r\nY")

TIMING_XBRK = """; Xpeccy+ Debugger breakpoints list
CPU:START::FL[:CNT::HL, BC
CPU:DONE::F]:CNT:DT > 11400
CPU:4000-57FF::W:CNT:
IO:00FE:0001:W:DBG:(VAL & 7) == 2
RAM:05:1800-1AFF:W:CNT:
COND:::E:DBG:SP < 0x5B00
"""


def main(out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "demo.tap").write_bytes(make_tap())
    (out / "demo.trd").write_bytes(make_trd())
    with zipfile.ZipFile(out / "demo.zip", "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in (("Demo - Side A.tap", make_tap()), ("Demo - Side B.tap", make_tap()),
                           ("Demo.trd", make_trd())):
            z.writestr(zipfile.ZipInfo(name, (2026, 10, 31, 12, 0, 0)), data, zipfile.ZIP_DEFLATED)
    (out / "timing.bin").write_bytes(TIMING_BIN)
    (out / "timing.lab").write_text(TIMING_LAB, encoding="ascii")
    (out / "timing.xbrk").write_text(TIMING_XBRK, encoding="ascii")
    (out / "exolon.lab").write_text(EXOLON_LAB, encoding="ascii")
    (out / "exolon.xbrk").write_text(EXOLON_XBRK, encoding="ascii")
    (out / "exolon.pok").write_bytes(EXOLON_POK.encode("ascii"))


if __name__ == "__main__":
    main(sys.argv[1])

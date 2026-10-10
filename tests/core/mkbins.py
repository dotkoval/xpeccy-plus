#!/usr/bin/env python3
"""Assemble the quick tier's Z80 programs into the committed bin/ folders.

  mkbins.py [bench...] [--sjasmplus PATH]

Each `bin` line of <bench>/cases.txt says the file, its source in asm/ and the
defines. A .spg is the program wrapped as an SPG 1.0 (one uncompressed block in
page 2, started at #8000). Needs sjasmplus: --sjasmplus, $SJASMPLUS or PATH.
regress.py quick never runs this - the .bin files are committed.
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
import quick  # noqa: E402

PROG_BENCHES = ("tsconf", "baseconf")


def find_sjasmplus(arg):
    for c in (arg, os.environ.get("SJASMPLUS"), shutil.which("sjasmplus")):
        if c and Path(c).is_file():
            return c
    return None


def spg(code):
    """A raw binary assembled at #8000 as an SPG 1.0: one uncompressed block, page 2."""
    nblk = (len(code) + 511) // 512
    code += b"\0" * (nblk * 512 - len(code))
    hd = bytearray(0x100)
    hd[0x20:0x2c] = b"SpectrumProg"
    hd[0x2c] = 0x10
    struct.pack_into("<HHBB", hd, 0x30, 0x8000, 0xbf00, 0, 0)	# pc, sp, page3, flags (3.5 MHz, DI)
    struct.pack_into("<H", hd, 0x3a, 1)
    blk = bytearray(768)
    blk[0:3] = bytes([0x80, nblk - 1, 2])				# last, #0000 of page 2, nblk * 512
    return bytes(hd) + bytes(blk) + code


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bench", nargs="*", help=f"default: {' '.join(PROG_BENCHES)}")
    ap.add_argument("--sjasmplus")
    args = ap.parse_args()
    sj = find_sjasmplus(args.sjasmplus)
    if not sj:
        sys.exit("no sjasmplus: pass --sjasmplus or set SJASMPLUS")
    changed = 0
    for bench in args.bench or PROG_BENCHES:
        bins, _ = quick.parse_cases(bench)
        asm = quick.CORE / bench / "asm"
        out = quick.CORE / bench / "bin"
        out.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="xbins-") as tmp:
            for file, src, defs in bins:
                raw = Path(tmp) / "out.bin"
                # sources include common.inc by a relative name, so assemble from asm/
                cmd = [sj, "--nologo", "--msg=err"] + [f"-D{d}" for d in defs] + [f"--raw={raw}", src]
                p = subprocess.run(cmd, cwd=asm)
                if p.returncode:
                    sys.exit(f"{bench}/{src}: sjasmplus failed")
                data = raw.read_bytes()
                if file.endswith(".spg"):
                    data = spg(data)
                dst = out / file
                if not dst.exists() or dst.read_bytes() != data:
                    dst.write_bytes(data)
                    changed += 1
                    print(f"wrote {bench}/bin/{file}")
    print(f"{changed} files changed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""Remake the 7z fixtures with 7-Zip: python make7z.py <path to 7z or 7zz>.

Python cannot write 7z, so these few small archives are kept in the repository.
"""

import os
from pathlib import Path
import subprocess
import sys
import tempfile

SEVEN = sys.argv[1]
HERE = Path(__file__).resolve().parent

# the same bytes every time, so a remake changes nothing that matters
SIDE_A = bytes((i * 7 + 3) & 0xff for i in range(20_000))
SIDE_B = bytes((i * 13 + 1) & 0xff for i in range(15_000))
CODE = bytes([0xe8, 0x10, 0x20, 0x00, 0x00, 0x90] * 4000)


def make(name, files, *switches):
    target = HERE / name
    if target.exists():
        target.unlink()
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        for path, data in files.items():
            (root / path).parent.mkdir(parents=True, exist_ok=True)
            if path.endswith("/"):
                (root / path).mkdir(exist_ok=True)
            else:
                (root / path).write_bytes(data)
        names = sorted({path.split("/")[0] for path in files})
        subprocess.run([SEVEN, "a", "-t7z", "-bd", "-y", *switches, str(target), *names],
                       cwd=root, check=True, stdout=subprocess.DEVNULL)


two = {"Side A.tzx": SIDE_A, "Side B.tzx": SIDE_B, "INFO/readme.txt": b"two sides\n"}
make("solid.7z", two, "-m0=LZMA2", "-ms=on")
make("plain.7z", two, "-m0=LZMA", "-ms=off")
make("bcj.7z", {"loader.bin": CODE}, "-m0=BCJ", "-m1=LZMA2")
make("ppmd.7z", {"Side A.tzx": SIDE_A}, "-m0=PPMd")
make("stored.7z", {"Side A.tzx": SIDE_A}, "-m0=Copy")
make("names.7z", {"В гнезде орла.tzx": SIDE_A,
                  "Edición/Side 1.tap": SIDE_B})
make("encrypted.7z", {"Side A.tzx": SIDE_A}, "-psecret")
make("encrypted-names.7z", {"Side A.tzx": SIDE_A}, "-psecret", "-mhe=on")
# a block that unpacks past the limit: zeros, so the archive stays small
make("huge.7z", {"zeros.wav": bytes(257 << 20)}, "-m0=LZMA2", "-mx=9")
for path in sorted(HERE.glob("*.7z")):
    print(f"{path.name}: {os.path.getsize(path)} bytes")

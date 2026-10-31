"""Exercise the production Qt/zlib reader; fixtures need no external archiver."""

import gzip
import io
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
import zlib

HELPER = str(Path(sys.argv.pop(1)).resolve())


def member(name, data=b"System/Next\n", kind=tarfile.REGTYPE, **pax):
    entry = tarfile.TarInfo(name)
    entry.type = kind
    entry.size = len(data) if kind in (tarfile.REGTYPE, tarfile.XHDTYPE) else 0
    entry.pax_headers = pax
    entry.linkname = "../../outside"
    return entry, data


def tar(entries, fmt=tarfile.PAX_FORMAT, **pax):
    output = io.BytesIO()
    with tarfile.open(fileobj=output, mode="w", format=fmt, pax_headers=pax) as stream:
        for entry, data in entries:
            stream.addfile(entry, io.BytesIO(data))
    return output.getvalue()


def checksum(header):
    header[148:156] = b" " * 8
    header[148:156] = f"{sum(header):06o}\0 ".encode()
    return header


def zipped(entries, method=zipfile.ZIP_DEFLATED):
    """entries: (name, data); a name ending in / is a folder."""
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", method) as stream:
        for name, data in entries:
            entry = zipfile.ZipInfo(name, date_time=(2024, 6, 1, 12, 0, 0))
            entry.compress_type = method
            entry.external_attr = (0o40755 if name.endswith("/") else 0o644) << 16
            stream.writestr(entry, data)
    return output.getvalue()


class Unpack(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="next unpack ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.archive = self.root / "releases \u043a\u0430\u0440\u0442\u0430" / "-System Next.tar.gz"
        self.archive.parent.mkdir()
        self.destination = self.root / "new card" / "unpacked"

    def check(self, raw, name, *, valid=True, cancel=0):
        path = self.archive.parent / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(raw)
        result = subprocess.run(
            [HELPER, str(path), str(self.destination), str(cancel)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15,
        )
        self.assertEqual(result.returncode, 0 if valid else 1, result.stderr.decode(errors="replace"))
        if not valid:
            self.assertTrue(result.stderr.strip())
        return result

    def extract(self, raw, *, valid=True, cancel=0, compressed=False):
        return self.check(raw if compressed else gzip.compress(raw), self.archive.name,
                          valid=valid, cancel=cancel)

    def extract_zip(self, raw, *, valid=True, cancel=0):
        return self.check(raw, "System Next.zip", valid=valid, cancel=cancel)

    def test_empty_archive(self):
        self.extract(tar([]))
        self.assertEqual(list(self.destination.iterdir()), [])

    def test_one_end_block(self):
        # one zero block ends a tar too: what follows is checked to be padding
        self.extract(b"\0" * 512)
        self.assertEqual(list(self.destination.iterdir()), [])

    def test_files_and_directories(self):
        payload = os.urandom(180_000)
        self.extract(tar([
            member(".", kind=tarfile.DIRTYPE),
            member("release/empty", b""),
            member("release/empty-dir", kind=tarfile.DIRTYPE),
            member("release/machines/next/enNextZX.rom", payload),
            member("release/.hidden", b"hidden"),
            member("release/\u0444\u0430\u0439\u043b with spaces", b"unicode"),
        ], comment="GitLab commit"))
        self.assertEqual((self.destination / "release/machines/next/enNextZX.rom").read_bytes(), payload)
        self.assertEqual((self.destination / "release/.hidden").read_bytes(), b"hidden")
        self.assertTrue((self.destination / "release/empty-dir").is_dir())

    def test_tar_formats_and_long_paths(self):
        for fmt in (tarfile.USTAR_FORMAT, tarfile.GNU_FORMAT, tarfile.PAX_FORMAT):
            with self.subTest(format=fmt):
                path = "release/" + "long/" * 30 + "file"
                self.destination = self.root / str(fmt)
                self.extract(tar([member(path)], fmt))
                self.assertEqual((self.destination / path).read_bytes(), b"System/Next\n")

    def test_pax_path_and_size(self):
        self.extract(tar([member("short", b"abc", path="release/long-name", size="3")]))
        self.assertEqual((self.destination / "release/long-name").read_bytes(), b"abc")

    def test_pax_empty_override(self):
        self.extract(tar([member("original", b"abc", path="", size="")],
                         path="global-name", size="30"))
        self.assertEqual((self.destination / "original").read_bytes(), b"abc")

    def test_pax_global_deletion(self):
        raw = (tar([member("first")], path="global-name")[:2048]
               + tar([member("second")], path=""))
        self.extract(raw)
        self.assertEqual((self.destination / "global-name").read_bytes(), b"System/Next\n")
        self.assertEqual((self.destination / "second").read_bytes(), b"System/Next\n")

    def test_binary_size(self):
        raw = bytearray(tar([member("file", b"abc")], tarfile.USTAR_FORMAT))
        raw[124:136] = (3).to_bytes(12, "big")
        raw[124] |= 0x80
        raw[:512] = checksum(raw[:512])
        self.extract(bytes(raw))
        self.assertEqual((self.destination / "file").read_bytes(), b"abc")

    def test_unsafe_paths(self):
        paths = ("../outside", "release/../../outside", "/outside", "C:/outside",
                 "C:outside", r"\\server\outside", r"release\..\outside", "file:stream",
                 "NUL", "release/COM1.txt", "CON .txt", "COM\u00b9", "CONOUT$",
                 "release/space ", "release/dot.", "")
        for index, path in enumerate(paths):
            with self.subTest(path=path):
                self.destination = self.root / f"unsafe-{index}"
                self.extract(tar([member(path)]), valid=False)
        self.assertFalse((self.root / "outside").exists())

    def test_pax_traversal(self):
        self.extract(tar([member("safe", path="../outside")]), valid=False)
        self.assertFalse((self.root / "outside").exists())

    def test_links_and_special_files(self):
        for kind in (tarfile.SYMTYPE, tarfile.LNKTYPE, tarfile.FIFOTYPE, tarfile.CHRTYPE):
            with self.subTest(type=kind):
                self.destination = self.root / kind.decode()
                self.extract(tar([member("release/link", kind=kind)]), valid=False)

    def test_existing_destination_is_preserved(self):
        self.destination.mkdir(parents=True)
        original = self.destination / "keep"
        original.write_bytes(b"do not overwrite")
        self.extract(tar([member("keep", b"changed")]), valid=False)
        self.assertEqual(original.read_bytes(), b"do not overwrite")

    def test_hidden_destination_is_preserved(self):
        self.destination.mkdir(parents=True)
        original = self.destination / ".keep"
        original.write_bytes(b"keep")
        self.extract(tar([member("file")]), valid=False)
        self.assertEqual(original.read_bytes(), b"keep")
        self.assertFalse((self.destination / "file").exists())

    @unittest.skipUnless(hasattr(os, "symlink") and os.name != "nt", "requires symlinks")
    def test_existing_symlink_is_rejected(self):
        self.destination.mkdir(parents=True)
        (self.destination / ".link").symlink_to(self.root, target_is_directory=True)
        self.extract(tar([member(".link/outside")]), valid=False)
        self.assertFalse((self.root / "outside").exists())

    @unittest.skipUnless(hasattr(os, "symlink") and os.name != "nt", "requires symlinks")
    def test_symlink_destination_is_rejected(self):
        outside = self.root / "outside"
        outside.mkdir()
        self.destination.parent.mkdir()
        self.destination.symlink_to(outside, target_is_directory=True)
        self.extract(tar([member("file")]), valid=False)
        self.assertEqual(list(outside.iterdir()), [])

    def test_duplicate_file(self):
        self.extract(tar([member("file"), member("file", b"replace")]), valid=False)
        self.assertEqual((self.destination / "file").read_bytes(), b"System/Next\n")

    def test_file_directory_collision(self):
        self.extract(tar([member("file"), member("file/child")]), valid=False)
        self.assertEqual((self.destination / "file").read_bytes(), b"System/Next\n")

    def test_invalid_utf8(self):
        raw = bytearray(tar([member("file")], tarfile.USTAR_FORMAT))
        raw[0] = 0xff
        raw[:512] = checksum(raw[:512])
        self.extract(bytes(raw), valid=False)

    def test_concatenated_gzip(self):
        raw = tar([member("file")])
        self.extract(gzip.compress(raw[:600]) + gzip.compress(raw[600:]), compressed=True)
        self.assertEqual((self.destination / "file").read_bytes(), b"System/Next\n")

    def test_gzip_errors(self):
        packed = gzip.compress(tar([member("file")]))
        damaged = bytearray(packed)
        damaged[-8] ^= 1
        for index, data in enumerate((b"not gzip", packed[:-8], bytes(damaged))):
            with self.subTest(case=index):
                self.destination = self.root / f"gzip-{index}"
                self.extract(data, valid=False, compressed=True)

    def test_tar_errors(self):
        raw = bytearray(tar([member("file")], tarfile.USTAR_FORMAT))
        bad_checksum = raw[:]
        bad_checksum[0] ^= 1
        bad_size = raw[:]
        bad_size[124:136] = b"99999999999\0"
        bad_size[:512] = checksum(bad_size[:512])
        overflowing_size = raw[:]
        overflowing_size[124:136] = (1 << 63).to_bytes(12, "big")
        overflowing_size[124] |= 0x80
        overflowing_size[:512] = checksum(overflowing_size[:512])
        for index, data in enumerate((raw[:600], bad_checksum, bad_size,
                                      overflowing_size, raw + b"extra tar data")):
            with self.subTest(case=index):
                self.destination = self.root / f"tar-{index}"
                self.extract(bytes(data), valid=False)

    def test_bad_pax(self):
        self.extract(tar([member("pax", b"999 path=bad\n", tarfile.XHDTYPE)]), valid=False)

    def test_empty_gnu_long_name(self):
        entry = member("long-name", b"", tarfile.GNUTYPE_LONGNAME)
        self.extract(tar([entry, member("file")], tarfile.GNU_FORMAT), valid=False)

    def test_large_metadata(self):
        self.extract(tar([member("pax", b"x" * 1_048_577, tarfile.XHDTYPE)]), valid=False)

    def test_negative_pax_size(self):
        self.extract(tar([member("file", size="-1")]), valid=False)

    def test_sparse_pax_is_rejected(self):
        self.extract(tar([member("sparse", b"abc", **{"GNU.sparse.map": "0,3",
                                                   "GNU.sparse.size": "10"})]), valid=False)

    def test_cancellation(self):
        result = self.extract(tar([member("large", os.urandom(2_000_000))]), valid=False, cancel=8)
        self.assertIn(b"cancelled", result.stderr)
        self.assertLess((self.destination / "large").stat().st_size, 2_000_000)

    def test_zip_files_and_directories(self):
        payload = os.urandom(180_000)
        self.extract_zip(zipped([
            ("release/", b""),
            ("release/empty", b""),
            ("release/empty-dir/", b""),
            ("release/machines/next/enNextZX.rom", payload),
            ("release/.hidden", b"hidden"),
            ("release/\u0444\u0430\u0439\u043b with spaces", b"unicode"),
            ("release/" + "long/" * 30 + "file", b"deep"),
        ]))
        self.assertEqual((self.destination / "release/machines/next/enNextZX.rom").read_bytes(), payload)
        self.assertEqual((self.destination / "release/.hidden").read_bytes(), b"hidden")
        self.assertTrue((self.destination / "release/empty-dir").is_dir())
        self.assertEqual((self.destination / ("release/" + "long/" * 30 + "file")).read_bytes(), b"deep")

    def test_zip_stored(self):
        payload = os.urandom(70_000)
        self.extract_zip(zipped([("release/stored", payload)], zipfile.ZIP_STORED))
        self.assertEqual((self.destination / "release/stored").read_bytes(), payload)

    def test_zip_unsafe_paths(self):
        paths = ("../outside", "release/../../outside", "/outside", "C:/outside",
                 "C:outside", "\\server\\outside", "file:stream", "NUL",
                 "release/COM1.txt", "release/space ", "release/dot.")
        for index, path in enumerate(paths):
            with self.subTest(path=path):
                self.destination = self.root / f"unsafe-zip-{index}"
                self.extract_zip(zipped([(path, b"x")]), valid=False)
        self.assertFalse((self.root / "outside").exists())

    def test_zip_bad_crc(self):
        raw = zipped([("release/file", b"payload")])
        # the CRC32 sits in the central directory and, again, in the local header
        for signature, offset in ((b"PK\x01\x02", 16), (b"PK\x03\x04", 14)):
            at = raw.index(signature) + offset
            raw = raw[:at] + bytes([raw[at] ^ 1]) + raw[at + 1:]
        self.extract_zip(raw, valid=False)

    def test_zip_damaged_deflate(self):
        raw = bytearray(zipped([("release/file", os.urandom(40_000))]))
        at = raw.index(b"PK\x03\x04") + 30 + len("release/file")
        raw[at + 4] ^= 0xff
        self.extract_zip(bytes(raw), valid=False)

    def test_zip_truncated(self):
        raw = zipped([("release/file", os.urandom(40_000))])
        for index, cut in enumerate((raw[:len(raw) // 2], raw[:-22], raw[:-200])):
            with self.subTest(cut=index):
                self.destination = self.root / f"zip-cut-{index}"
                self.extract_zip(cut, valid=False)

    def test_zip_entry_past_its_size(self):
        # 5 MB of zeros deflate to a few KB; the entry claims 10 bytes in both headers
        raw = bytearray(zipped([("release/file", bytes(5_000_000))]))
        for signature, offset in ((b"PK\x01\x02", 24), (b"PK\x03\x04", 22)):
            at = raw.index(signature) + offset
            raw[at:at + 4] = (10).to_bytes(4, "little")
        self.extract_zip(bytes(raw), valid=False)
        self.assertLessEqual((self.destination / "release/file").stat().st_size, 10)

    def test_zip_encrypted(self):
        raw = bytearray(zipped([("release/file", b"payload")]))
        for signature, offset in ((b"PK\x01\x02", 8), (b"PK\x03\x04", 6)):
            at = raw.index(signature) + offset
            raw[at] |= 1
        self.extract_zip(bytes(raw), valid=False)

    def test_zip64_is_refused(self):
        raw = bytearray(zipped([("release/file", b"payload")]))
        at = raw.index(b"PK\x01\x02") + 24
        raw[at:at + 4] = b"\xff\xff\xff\xff"
        self.extract_zip(bytes(raw), valid=False)

    def test_zip_names_disagree(self):
        raw = bytearray(zipped([("release/fileA", b"payload")]))
        at = raw.index(b"PK\x03\x04") + 30
        raw[at:at + len("release/fileA")] = b"release/fileB"
        self.extract_zip(bytes(raw), valid=False)

    def test_zip_duplicate(self):
        output = io.BytesIO()
        with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as stream:
            stream.writestr("file", b"first")
            stream.writestr("file", b"second")
        self.extract_zip(output.getvalue(), valid=False)
        self.assertFalse((self.destination / "file").exists())

    def test_zip_existing_destination_is_preserved(self):
        self.destination.mkdir(parents=True)
        original = self.destination / "keep"
        original.write_bytes(b"do not overwrite")
        self.extract_zip(zipped([("keep", b"changed")]), valid=False)
        self.assertEqual(original.read_bytes(), b"do not overwrite")

    def test_zip_cancellation(self):
        result = self.extract_zip(zipped([("large", os.urandom(2_000_000))]), valid=False, cancel=8)
        self.assertIn(b"cancelled", result.stderr)
        self.assertLess((self.destination / "large").stat().st_size, 2_000_000)


# what data/make7z.py packed into the 7z fixtures
SIDE_A = bytes((i * 7 + 3) & 0xff for i in range(20_000))
SIDE_B = bytes((i * 13 + 1) & 0xff for i in range(15_000))
CODE = bytes([0xe8, 0x10, 0x20, 0x00, 0x00, 0x90] * 4000)
DATA = Path(__file__).resolve().parent / "data"
LIMIT = 256 << 20


def renamed(raw, old, new, utf8_flag=None):
    """The zip with an entry's name bytes swapped in both headers (same length)."""
    assert len(old) == len(new)
    raw = bytearray(raw)
    for signature, flags in ((b"PK\x03\x04", 6), (b"PK\x01\x02", 8)):
        at = raw.index(signature)
        name = raw.index(old, at)
        raw[name:name + len(old)] = new
        if utf8_flag is not None:
            raw[at + flags + 1] = (raw[at + flags + 1] & ~0x08) | (0x08 if utf8_flag else 0)
    return bytes(raw)


class Media(unittest.TestCase):
    """The readers an image in an archive is opened with: list, then one entry out."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="media \u0430\u0440\u0445\u0438\u0432 ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.out = self.root / "out" / "Side A.tzx"
        self.out.parent.mkdir()

    def run_helper(self, *args):
        return subprocess.run([HELPER, *map(str, args)], stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=60)

    def put(self, name, raw):
        path = self.root / name
        path.write_bytes(raw)
        return path

    def listing(self, path, valid=True):
        result = self.run_helper("--list", path)
        self.assertEqual(result.returncode, 0 if valid else 1, result.stderr.decode(errors="replace"))
        if not valid:
            self.assertTrue(result.stderr.strip())
            return None
        rows = [line.split("\t", 1) for line in result.stdout.decode("utf-8").splitlines()]
        return {name: int(size) for size, name in rows}

    def take(self, path, entry, valid=True):
        result = self.run_helper("--extract", path, entry, self.out)
        self.assertEqual(result.returncode, 0 if valid else 1, result.stderr.decode(errors="replace"))
        if valid:
            return self.out.read_bytes()
        self.assertTrue(result.stderr.strip())
        self.assertFalse(self.out.exists(), "a failed entry left a file behind")
        return result.stderr

    # zip

    def test_zip_list_and_one_entry(self):
        path = self.put("game.zip", zipped([
            ("Side A.tzx", SIDE_A), ("Side B.tzx", SIDE_B), ("SCRSHOT/", b""),
            ("SCRSHOT/game.scr", bytes(6912)), ("Side A.tzx", b"a second copy")]))
        self.assertEqual(self.listing(path), {"Side A.tzx": len(SIDE_A), "Side B.tzx": len(SIDE_B),
                                              "SCRSHOT/": 0, "SCRSHOT/game.scr": 6912})
        self.assertEqual(self.take(path, "Side B.tzx"), SIDE_B)
        self.assertEqual(self.take(path, "Side A.tzx"), SIDE_A)

    def test_zip_names(self):
        raw = zipped([("XXXXXX.tzx", SIDE_A)])
        cases = (
            # flagged UTF-8, UTF-8 with no flag, CP866 (letters together), CP437 (one alone)
            ("\u0444\u0430\u0439.tzx", "utf-8", True),
            ("\u00e9t\u00e9x.tzx", "utf-8", False),
            ("\u041c\u042d\u0419\u0425\u0415\u041c.tzx", "cp866", False),
            ("Acci\u00f3n.tzx", "cp437", False),
        )
        for index, (text, codec, flag) in enumerate(cases):
            with self.subTest(name=text):
                path = self.put(f"names-{index}.zip",
                                renamed(raw, b"XXXXXX.tzx", text.encode(codec), flag))
                self.assertEqual(list(self.listing(path)), [text])
                self.assertEqual(self.take(path, text), SIDE_A)

    def test_zip_over_the_limit(self):
        raw = bytearray(zipped([("zeros.wav", bytes(1000))]))
        for signature, offset in ((b"PK\x01\x02", 24), (b"PK\x03\x04", 22)):
            at = raw.index(signature) + offset
            raw[at:at + 4] = (LIMIT + 1).to_bytes(4, "little")
        path = self.put("big.zip", bytes(raw))
        self.assertEqual(self.listing(path), {"zeros.wav": LIMIT + 1})
        self.assertIn(b"MB", self.take(path, "zeros.wav", valid=False))

    def test_zip_refusals(self):
        good = zipped([("Side A.tzx", SIDE_A)])
        encrypted = bytearray(good)
        for signature, offset in ((b"PK\x01\x02", 8), (b"PK\x03\x04", 6)):
            encrypted[encrypted.index(signature) + offset] |= 1
        bad_crc = bytearray(good)
        at = bad_crc.index(b"PK\x01\x02") + 16
        bad_crc[at] ^= 1
        for index, raw in enumerate((bytes(encrypted), good[:-30], b"not a zip at all")):
            with self.subTest(case=index):
                self.listing(self.put(f"bad-{index}.zip", raw), valid=False)
        self.take(self.put("crc.zip", bytes(bad_crc)), "Side A.tzx", valid=False)
        self.take(self.put("good.zip", good), "Side B.tzx", valid=False)

    # gz

    def gz(self, data, fname=""):
        output = io.BytesIO()
        with gzip.GzipFile(filename=fname, mode="wb", fileobj=output, mtime=0) as stream:
            stream.write(data)
        return output.getvalue()

    def test_gz_names(self):
        # the archive's own name, unless nothing is left of it but a bare word
        path = self.put("Game (Side A).tzx.gz", self.gz(SIDE_A, "game.tzx"))
        self.assertEqual(self.listing(path), {"Game (Side A).tzx": len(SIDE_A)})
        self.assertEqual(self.take(path, "Game (Side A).tzx"), SIDE_A)
        path = self.put("game.gz", self.gz(SIDE_B, "C:\\files\\inner.tap"))
        self.assertEqual(list(self.listing(path)), ["inner.tap"])
        self.assertEqual(self.take(path, "inner.tap"), SIDE_B)
        path = self.put("bare.gz", self.gz(SIDE_B))
        self.assertEqual(list(self.listing(path)), ["bare"])

    def test_gz_streams_and_padding(self):
        path = self.put("two.tap.gz", self.gz(SIDE_A[:5000]) + self.gz(SIDE_A[5000:]) + bytes(16))
        self.assertEqual(self.take(path, "two.tap"), SIDE_A)

    def test_gz_errors(self):
        packed = self.gz(SIDE_A)
        damaged = bytearray(packed)
        damaged[-8] ^= 1
        for index, raw in enumerate((packed[:-8], bytes(damaged), packed + b"garbage")):
            with self.subTest(case=index):
                self.take(self.put(f"bad-{index}.tap.gz", raw), f"bad-{index}.tap", valid=False)
        self.listing(self.put("not.tap.gz", b"not gzip"), valid=False)

    def test_gz_over_the_limit(self):
        pack = zlib.compressobj(9, zlib.DEFLATED, 31)
        chunk = bytes(1 << 20)
        raw = b"".join(pack.compress(chunk) for _ in range((LIMIT >> 20) + 1)) + pack.flush()
        self.assertIn(b"MB", self.take(self.put("zeros.wav.gz", raw), "zeros.wav", valid=False))

    # 7z, from data/

    def test_7z_methods(self):
        for name in ("solid.7z", "plain.7z"):
            with self.subTest(archive=name):
                self.assertEqual(self.listing(DATA / name), {
                    "Side A.tzx": len(SIDE_A), "Side B.tzx": len(SIDE_B),
                    "INFO/": 0, "INFO/readme.txt": 10})
                self.assertEqual(self.take(DATA / name, "Side B.tzx"), SIDE_B)
                self.assertEqual(self.take(DATA / name, "Side A.tzx"), SIDE_A)
        self.assertEqual(self.take(DATA / "bcj.7z", "loader.bin"), CODE)
        self.assertEqual(self.take(DATA / "ppmd.7z", "Side A.tzx"), SIDE_A)
        self.assertEqual(self.take(DATA / "stored.7z", "Side A.tzx"), SIDE_A)

    def test_7z_names(self):
        russian = "\u0412 \u0433\u043d\u0435\u0437\u0434\u0435 \u043e\u0440\u043b\u0430.tzx"
        self.assertEqual(set(self.listing(DATA / "names.7z")),
                         {russian, "Edici\u00f3n/", "Edici\u00f3n/Side 1.tap"})
        self.assertEqual(self.take(DATA / "names.7z", russian), SIDE_A)
        self.assertEqual(self.take(DATA / "names.7z", "Edici\u00f3n/Side 1.tap"), SIDE_B)

    def test_7z_refusals(self):
        self.take(DATA / "encrypted.7z", "Side A.tzx", valid=False)
        self.listing(DATA / "encrypted-names.7z", valid=False)
        self.assertIn(b"MB", self.take(DATA / "huge.7z", "zeros.wav", valid=False))
        raw = (DATA / "solid.7z").read_bytes()
        self.listing(self.put("cut.7z", raw[:len(raw) // 2]), valid=False)
        self.listing(self.put("junk.7z", b"7z but not"), valid=False)
        self.take(DATA / "solid.7z", "Side C.tzx", valid=False)
        # any byte of it flipped: refused or, where nothing checks it, read - never a crash
        for at in range(32, len(raw), 7):
            damaged = bytearray(raw)
            damaged[at] ^= 0x5a
            path = self.put("damaged.7z", bytes(damaged))
            for args in (("--list", path), ("--extract", path, "Side A.tzx", self.out)):
                self.assertIn(self.run_helper(*args).returncode, (0, 1), f"byte {at}")
            self.out.unlink(missing_ok=True)


if __name__ == "__main__":
    unittest.main()

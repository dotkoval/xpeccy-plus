# LZMA SDK in Xpeccy+

Images packed in a `.7z` archive are opened with the 7z reader of the LZMA SDK, built from
this copy on every platform (`src/xcore/archive.cpp` uses it).

- Release: LZMA SDK 26.05, `lzma2605.7z` from
  https://github.com/ip7z/7zip/releases/download/26.05/lzma2605.7z (linked from
  https://www.7-zip.org/sdk.html), sha256
  `668fda469b7f584f246008cf28359c66ef98d65fafb3b22a52672eebe6970c28`, the digest the
  GitHub release publishes for it.
- Kept, unchanged: `DOC/lzma-sdk.txt` (the license statement) and, from `C/`, the files the
  SDK's own 7z extractor (`C/Util/7z/makefile.gcc`) builds, less its file and allocator
  helpers: `7zArcIn.c 7zBuf.c 7zCrc.c 7zCrcOpt.c 7zDec.c 7zStream.c Bcj2.c Bra.c Bra86.c
  BraIA64.c CpuArch.c Delta.c Lzma2Dec.c LzmaDec.c Ppmd7.c Ppmd7Dec.c` and the headers they
  include. Everything else of the release is left out; no assembler.
- Built as the static library `xplzma` with `Z7_PPMD_SUPPORT` and `Z7_EXTRACT_ONLY`, as
  that makefile does. Our reader reads the archive through its own stream and allocator.
- License: public domain (`DOC/lzma-sdk.txt`), credited in `LICENSE_LZMA`.

To update: unpack the new release, replace the kept files, add any file the 7z extractor's
makefile gained, check the archive tests (`tests/sysnext`).

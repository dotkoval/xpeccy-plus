#!/bin/sh
# Copyright (c) 2026 Oleksandr Kovalchuk. GNU GPL v2 or later - see LICENSE.
# A development tool, not part of the emulator; see coretest.c for where it
# comes from.

# Build the Z80 core test and run it against fuse's timing tests.
#
#   ./run.sh
#
# The test data ships in tests/; FUSE_Z80_TESTS points it somewhere else, such
# as the z80/tests folder of a fuse checkout. CC=gcc by default; on Windows the
# Qt toolchain works:
#
#   CC=C:/Qt/Tools/mingw1120_64/bin/gcc.exe ./run.sh

set -e
cd "$(dirname "$0")"

: "${CC:=gcc}"
: "${FUSE_Z80_TESTS:=tests}"

if [ ! -f "$FUSE_Z80_TESTS/tests.in" ]; then
	echo "no tests.in under $FUSE_Z80_TESTS - set FUSE_Z80_TESTS" >&2
	exit 1
fi

"$CC" -O1 -o coretest coretest.c ../../src/libxpeccy/cpu/Z80/*.c
./coretest "$FUSE_Z80_TESTS/tests.in" > out.txt
exec python compare.py out.txt "$FUSE_Z80_TESTS/tests.expected"

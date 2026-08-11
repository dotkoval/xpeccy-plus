#!/bin/sh
# Checks that a build starts, finds its own machine configuration and reaches
# the end of main(). --help does all of that: it builds the windows and reads
# the configuration, it just never shows anything or enters the event loop.
#
#   packaging/linux-smoke.sh <binary or .AppImage> [config dir]
#
# Qt creates the QApplication before anyone looks at the arguments, so even
# --help needs a display; without one the run goes through xvfb-run.

set -e

BIN="$1"
CONF="${2:-${TMPDIR:-/tmp}/xpeccy-smoke-conf}"
LOG="${TMPDIR:-/tmp}/xpeccy-smoke.log"
TIMEOUT="${TIMEOUT:-60}"

[ -x "$BIN" ] || { echo "no binary at $BIN"; exit 1; }
rm -rf "$CONF"

RUN=""
if [ -z "$DISPLAY" ]; then
	command -v xvfb-run > /dev/null || { echo "no display and no xvfb-run"; exit 1; }
	RUN="xvfb-run -a"
fi

set +e
# shellcheck disable=SC2086
$RUN timeout "$TIMEOUT" "$BIN" --confdir "$CONF" --help > "$LOG" 2>&1
rc=$?
set -e

cat "$LOG"

if [ "$rc" = 124 ]; then echo "still running after ${TIMEOUT}s"; exit 1; fi
# what main() prints on its way out, so a crash cannot pass for success
grep -q "^exit" "$LOG" || { echo "did not reach the end of main()"; exit 1; }
[ "$rc" = 0 ] || { echo "exited with $rc - main() finished, so this is a destructor"; exit 1; }
[ -s "$CONF/config.conf" ] || { echo "no config.conf: the bundled configuration was not found"; exit 1; }
roms=$(ls -1 "$CONF/roms" | wc -l)
echo "roms seeded: $roms"
[ "$roms" -ge 20 ] || { echo "the rom images did not make it into the package"; exit 1; }

echo "ok: $BIN"

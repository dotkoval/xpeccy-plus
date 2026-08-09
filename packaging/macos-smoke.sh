#!/bin/sh
# Checks that a bundle starts, finds its own machine configuration and reaches
# the end of main(). --help does all of that: it builds the windows and reads
# the configuration, it just never shows anything or enters the event loop.
#
#   packaging/macos-smoke.sh <path to .app> [config dir]
#
# On a hang - a modal dialog is the usual reason, and there is nobody to click
# it away on a build machine - it saves a screenshot and a stack next to the log
# in $TMPDIR before giving up.

set -e

APP="$1"
CONF="${2:-${TMPDIR:-/tmp}/xpeccy-smoke-conf}"
NAME=xpeccy-plus
BIN="$APP/Contents/MacOS/$NAME"
LOG="${TMPDIR:-/tmp}/xpeccy-smoke.log"
SHOT="${TMPDIR:-/tmp}/xpeccy-hang.png"
TIMEOUT="${TIMEOUT:-60}"

[ -x "$BIN" ] || { echo "no binary at $BIN"; exit 1; }
rm -rf "$CONF"

# a pty, so the app's own output arrives line by line and survives a kill
script -q /dev/null "$BIN" --confdir "$CONF" --help > "$LOG" 2>&1 &
runner=$!

i=0
while [ "$i" -lt "$TIMEOUT" ] && kill -0 $runner 2>/dev/null; do
	sleep 1
	i=$((i + 1))
done

if kill -0 $runner 2>/dev/null; then
	echo "still running after ${TIMEOUT}s"
	screencapture -x "$SHOT" 2>/dev/null && echo "screen: $SHOT"
	sample "$(pgrep -x $NAME)" 2 -file /dev/stdout 2>&1 | head -n 60 || true
	pkill -9 -x $NAME || true
	kill -9 $runner 2>/dev/null || true
	cat "$LOG"
	exit 1
fi

cat "$LOG"

# what main() prints on its way out, so a crash cannot pass for success
grep -q "^exit" "$LOG" || { echo "did not reach the end of main()"; exit 1; }
[ -s "$CONF/config.conf" ] || { echo "no config.conf: the bundled configuration was not found"; exit 1; }
roms=$(ls -1 "$CONF/roms" | wc -l | tr -d ' ')
echo "roms seeded: $roms"
[ "$roms" -ge 20 ] || { echo "the rom images did not make it into the bundle"; exit 1; }

echo "ok: $APP"

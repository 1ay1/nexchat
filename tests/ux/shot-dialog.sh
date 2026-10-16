#!/bin/sh
# Screenshot one dialog of a nexchat build under a private Xvfb.
# usage: tests/ux/shot-dialog.sh BIN OUT.png "/COMMAND" [extra hexchat.conf lines]
#        KEYS="ctrl+s" to press keys (tests/ux/xkey.py) after startup
# Opens the dialog with -c and/or KEYS, waits, grabs the root window.
set -eu
bin=$1; out=$2; cmd=$3; extra=${4:-}
here=$(cd "$(dirname "$0")" && pwd)
xvfb=${XVFB:-/opt/X11/bin/Xvfb}
xwd=${XWD:-/opt/X11/bin/xwd}
disp=${SHOT_DISPLAY:-:94}
tmp=$(mktemp -d)
trap 'kill $app $xv 2>/dev/null || true; rm -rf "$tmp"' EXIT

"$xvfb" "$disp" -screen 0 1100x720x24 -nolisten tcp >/dev/null 2>&1 &
xv=$!
sleep 1.5
mkdir -p "$tmp/cfg"
printf 'gui_slist_skip = 1\nirc_nick1 = shot\ngui_win_left = 0\ngui_win_top = 0\n%s\n' "$extra" > "$tmp/cfg/hexchat.conf"
if [ -n "$cmd" ]; then
	DISPLAY=$disp "$bin" -a -d "$tmp/cfg" -c "$cmd" >"$tmp/log" 2>&1 &
else
	DISPLAY=$disp "$bin" -a -d "$tmp/cfg" >"$tmp/log" 2>&1 &
fi
app=$!
sleep "${SHOT_WAIT:-5}"
if [ -n "${KEYS:-}" ]; then
	# shellcheck disable=SC2086
	DISPLAY=$disp python3 "$here/xkey.py" --delay 0.6 $KEYS
	sleep 2
fi
DISPLAY=$disp "$xwd" -root -silent -out "$tmp/s.xwd"
python3 "$here/xwd2png.py" "$tmp/s.xwd" "$out"
if grep -qiE 'critical|segfault' "$tmp/log"; then grep -iE 'critical|warning' "$tmp/log" | sort | uniq -c | head; fi
kill -0 $app 2>/dev/null || echo "WARNING: client exited early" >&2

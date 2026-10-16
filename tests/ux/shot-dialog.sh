#!/bin/sh
# Screenshot one dialog of a nexchat build under a private Xvfb.
# usage: tests/ux/shot-dialog.sh BIN OUT.png "/COMMAND" [extra hexchat.conf lines]
#        KEYS="ctrl+s" to press keys (tests/ux/xkey.py) after startup
# Opens the dialog with -c and/or KEYS, waits, grabs the root window.
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1; out=$2; cmd=$3; extra=${4:-}
here=$(cd "$(dirname "$0")" && pwd)
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
xwd=${XWD:-$(command -v xwd || echo /opt/X11/bin/xwd)}
disp=${SHOT_DISPLAY:-:94}
tmp=$(mktemp -d)
app=; ircd=
trap 'kill $app $xv $ircd 2>/dev/null || true; rm -rf "$tmp"' EXIT

start_xvfb "$xvfb" "$disp"
mkdir -p "$tmp/cfg"
printf 'gui_slist_skip = 1\nirc_nick1 = shot\ngui_win_left = 0\ngui_win_top = 0\n%s\n' "$extra" > "$tmp/cfg/hexchat.conf"
# SHOT_FILES=dir: more config files (notify.conf, ...) copied in
if [ -n "${SHOT_FILES:-}" ]; then cp "$SHOT_FILES"/* "$tmp/cfg/"; fi
# SHOT_CONNECT=1: join #nexchat on tests/fake_ircd.py --demo first
url=
if [ -n "${SHOT_CONNECT:-}" ]; then
	printf 'net_throttle = 0\n' >> "$tmp/cfg/hexchat.conf"
	python3 "$here/../fake_ircd.py" --demo --timeout 60 --log "$tmp/server.log" --portfile "$tmp/port" 2>/dev/null &
	ircd=$!
	i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
	url="irc://127.0.0.1:-$(cat "$tmp/port")/#nexchat"
fi
if [ -n "$cmd" ]; then
	DISPLAY=$disp "$bin" -a -d "$tmp/cfg" -c "$cmd" $url >"$tmp/log" 2>&1 &
else
	DISPLAY=$disp "$bin" -a -d "$tmp/cfg" $url >"$tmp/log" 2>&1 &
fi
app=$!
sleep "${SHOT_WAIT:-5}"
if [ -n "${KEYS:-}" ]; then
	# pointer on the input box (window is 640x400), away from where menus
	# drop down: a menu under the pointer takes the hover
	# shellcheck disable=SC2086
	DISPLAY=$disp python3 "$here/xkey.py" --delay 0.6 --at 400,380 $KEYS
	sleep 2
fi
DISPLAY=$disp "$xwd" -root -silent -out "$tmp/s.xwd"
python3 "$here/xwd2png.py" "$tmp/s.xwd" "$out"
if grep -qiE 'critical|segfault' "$tmp/log"; then grep -iE 'critical|warning' "$tmp/log" | sort | uniq -c | head; fi
kill -0 $app 2>/dev/null || echo "WARNING: client exited early" >&2

#!/bin/sh
# Screenshot a nexchat build after pressing some keys, headless.
#
# usage: tests/ux/shot.sh BIN OUT.png [KEY ...]
#   env: DISP=:93          X display to use (run several in parallel on different ones)
#        CONNECT=1         start tests/fake_ircd.py --demo and join #nexchat first
#        CONF="a = 1\n..." extra hexchat.conf lines
#        WAIT=7            seconds before pressing keys
#        AT=450,580        where to park the pointer (gives that window focus)
#        TYPE="/list"      literal text to type before the KEYs
# Keys are xkey.py combos: alt+w, Down, Return, slash, ...
# Needs Xvfb + xwd (XQuartz on macOS, xvfb/x11-apps on Linux); no xdotool.
set -eu
bin=$1; out=$2; shift 2
here=$(cd "$(dirname "$0")" && pwd)
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
xwd=${XWD:-$(command -v xwd || echo /opt/X11/bin/xwd)}
disp=${DISP:-:93}
tmp=$(mktemp -d)
app=; ircd=; xv=
cleanup() { kill $app $ircd $xv 2>/dev/null || true; wait 2>/dev/null || true
	if [ -n "${KEEP:-}" ]; then cp -r "$tmp" "$KEEP"; fi; rm -rf "$tmp"; }
trap cleanup EXIT

"$xvfb" "$disp" -screen 0 1100x720x24 -nolisten tcp >/dev/null 2>&1 &
xv=$!
sleep 1.5
mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = smoke\ngui_slist_skip = 1\ngui_win_width = 900\ngui_win_height = 600\ngui_win_left = 0\ngui_win_top = 0\n' > "$tmp/cfg/hexchat.conf"
if [ -n "${CONF:-}" ]; then
	# CONF entries replace the defaults above: drop base lines for the same keys
	printf '%b\n' "$CONF" > "$tmp/conf.extra"
	sed -E 's/ *=.*//' "$tmp/conf.extra" > "$tmp/conf.keys"
	grep -v -F -f "$tmp/conf.keys" "$tmp/cfg/hexchat.conf" > "$tmp/conf.base" || true
	cat "$tmp/conf.base" "$tmp/conf.extra" > "$tmp/cfg/hexchat.conf"
fi

if [ -n "${CONNECT:-}" ]; then
	python3 "$here/../fake_ircd.py" --demo --timeout 60 --log "$tmp/server.log" --portfile "$tmp/port" 2>/dev/null &
	ircd=$!
	i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
	DISPLAY=$disp "$bin" -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/#nexchat" >"$tmp/log" 2>&1 &
else
	DISPLAY=$disp "$bin" -a -d "$tmp/cfg" >"$tmp/log" 2>&1 &
fi
app=$!
if [ -n "${CONNECT:-}" ]; then
	# wait until the client really joined (parallel runs are slower)
	i=0; while ! grep -q '^JOIN' "$tmp/server.log" 2>/dev/null && [ $i -lt 150 ]; do sleep 0.1; i=$((i+1)); done
fi
sleep "${WAIT:-5}"
if [ $# -gt 0 ] || [ -n "${TYPE:-}" ]; then
	if [ -n "${TYPE:-}" ]; then
		DISPLAY=$disp python3 "$here/xkey.py" --delay 0.8 --at "${AT:-450,580}" --type "$TYPE" "$@"
	else
		DISPLAY=$disp python3 "$here/xkey.py" --delay 0.8 --at "${AT:-450,580}" "$@"
	fi
	sleep 2
fi
DISPLAY=$disp "$xwd" -root -silent -out "$tmp/s.xwd"
python3 "$here/xwd2png.py" "$tmp/s.xwd" "$out"
kill -0 "$app" 2>/dev/null || echo "WARNING: $bin exited early" >&2
grep -iE 'critical|gtk-warning' "$tmp/log" | sort | uniq -c | head -5 >&2 || true

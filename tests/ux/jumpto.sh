#!/bin/sh
# Window > Jump To (Ctrl+J): type part of a channel name, Enter, and the
# next line goes to that channel.
# usage: tests/ux/jumpto.sh path/to/nexchat     (needs Xvfb + XTest)
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:95}
tmp=$(mktemp -d)
app=; ircd=; xv=
trap 'kill $app $ircd $xv 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"' EXIT

start_xvfb "$xvfb" "$disp"
mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = smoke\ngui_slist_skip = 1\ngui_win_left = 0\ngui_win_top = 0\ngui_win_width = 900\ngui_win_height = 600\n' > "$tmp/cfg/hexchat.conf"
python3 "$here/../fake_ircd.py" --timeout 60 --log "$tmp/server.log" --portfile "$tmp/port" 2>/dev/null &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
DISPLAY=$disp "$bin" -a -d "$tmp/cfg" >"$tmp/log" 2>&1 &
app=$!
wait_window "$disp"

k() { DISPLAY=$disp python3 "$here/xkey.py" --delay 0.4 --at 450,580 "$@"; }
t() { DISPLAY=$disp python3 "$here/xkey.py" --delay 0.4 --at 450,580 --type "$1"; }

# #alpha first, #bravo last, so #bravo is the one in front
t "/server irc://127.0.0.1:-$(cat "$tmp/port")/#alpha,#bravo"; k Return
i=0; while [ "$(grep -c '^JOIN' "$tmp/server.log" 2>/dev/null || echo 0)" -lt 1 ] && [ $i -lt 150 ]; do sleep 0.1; i=$((i+1)); done
sleep 2

t "in-bravo"; k Return
# no WM: keys go to the window under the pointer, and Jump To opens there
k ctrl+j; sleep 1
t "alp"
k Return
sleep 1
t "in-alpha"; k Return
sleep 2

fail=0
expect() {
	if grep -qa -- "$1" "$tmp/server.log"; then echo "ok:   $2"; else echo "FAIL: $2"; fail=1; fi
}
expect '^PRIVMSG #bravo :in-bravo' 'starts in #bravo'
expect '^PRIVMSG #alpha :in-alpha' 'Ctrl+J alp Enter goes to #alpha'
if [ $fail -ne 0 ]; then
	echo "--- server.log"; cat "$tmp/server.log"
	echo "--- app log"; tail -20 "$tmp/log"
	exit 1
fi
echo "jumpto: ok"

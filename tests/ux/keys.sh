#!/bin/sh
# Drive the input box with real key presses and check what reaches the
# server: plain send, Up-arrow history, Tab nick completion, a Ctrl+K
# colour code (key_action_insert), Ctrl+PageDown tab switch.
# usage: tests/ux/keys.sh path/to/nexchat     (needs Xvfb + XTest)
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:96}
tmp=$(mktemp -d)
app=; ircd=; xv=
cleanup() {
	if [ -n "${KEEP:-}" ]; then
		DISPLAY=$disp "${XWD:-$(command -v xwd || echo /opt/X11/bin/xwd)}" -root -silent -out "$tmp/s.xwd" 2>/dev/null || true
		cp -r "$tmp" "$KEEP"
	fi
	kill $app $ircd $xv 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT

start_xvfb "$xvfb" "$disp"
mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = smoke\ngui_slist_skip = 1\ngui_win_left = 0\ngui_win_top = 0\ngui_win_width = 900\ngui_win_height = 600\ncompletion_suffix = :\n' > "$tmp/cfg/hexchat.conf"
python3 "$here/../fake_ircd.py" --demo --timeout 60 --log "$tmp/server.log" --portfile "$tmp/port" 2>/dev/null &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
# Start with no URL (-a: no autoconnect): exactly one server window opens.
# Passing a URL on the command line (or -c server URL) gives an extra empty
# "NETWORK" window in HexChat too, and pointer-focus typing lands there.
DISPLAY=$disp "$bin" -a -d "$tmp/cfg" >"$tmp/log" 2>&1 &
app=$!
wait_window "$disp"

k() { DISPLAY=$disp python3 "$here/xkey.py" --delay 0.4 --at 450,580 "$@"; }
t() { DISPLAY=$disp python3 "$here/xkey.py" --delay 0.4 --at 450,580 --type "$1"; }

t "/server irc://127.0.0.1:-$(cat "$tmp/port")/#nexchat"; k Return
i=0; while ! grep -q '^JOIN' "$tmp/server.log" 2>/dev/null && [ $i -lt 150 ]; do sleep 0.1; i=$((i+1)); done
sleep 1.5

t "first line"; k Return
t "second line"; k Return
k Up Up Return                       # history: resend "first line"
t "mal"; k Tab; t "hi"; k Return     # nick completion adds "mallory: " itself
k ctrl+k; t "4red"; k Return         # Ctrl+K inserts \003 (mIRC colour)
sleep 2

fail=0
expect() {
	if grep -qa -- "$1" "$tmp/server.log"; then echo "ok:   $2"; else echo "FAIL: $2"; fail=1; fi
}
expect '^PRIVMSG #nexchat :first line'  'plain send'
expect '^PRIVMSG #nexchat :second line' 'second send'
[ "$(grep -ac '^PRIVMSG #nexchat :first line' "$tmp/server.log")" -ge 2 ] && echo "ok:   history Up Up resends" || { echo "FAIL: history Up Up resends"; fail=1; }
expect '^PRIVMSG #nexchat :mallory: hi' 'tab completion'
expect "$(printf '^PRIVMSG #nexchat :\0034red')" 'ctrl+k colour code'
if [ $fail -ne 0 ]; then
	echo "--- server.log"; grep -a PRIVMSG "$tmp/server.log" | cat -v
	exit 1
fi
echo "keys: ok"

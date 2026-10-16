#!/bin/sh
# Smoke test for the graphical client on any backend (no X server, no
# XTest: works on Quartz/Wayland/X11 alike): start it with an irc:// URL
# and a command, check the fake ircd sees NICK/JOIN/the message, and that
# the main window renders (NEXCHAT_SHOT writes it as a PNG).
# Criticals are fatal (G_DEBUG=fatal-criticals).
# usage: tests/smoke-gui.sh BIN
set -eu
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
app=; ircd=
cleanup() { kill $app $ircd 2>/dev/null || true; wait 2>/dev/null || true
	if [ -n "${KEEP:-}" ]; then cp -r "$tmp" "$KEEP"; fi; rm -rf "$tmp"; }
trap cleanup EXIT

python3 "$here/fake_ircd.py" --log "$tmp/server.log" --portfile "$tmp/port" --timeout 60 &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
port=$(cat "$tmp/port")

mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = smoke\ngui_slist_skip = 1\ngui_win_width = 900\ngui_win_height = 600\n' > "$tmp/cfg/hexchat.conf"
G_DEBUG=fatal-criticals NEXCHAT_SHOT="$tmp/shot.png:6" \
	"$bin" -a -d "$tmp/cfg" "irc://127.0.0.1:-$port/#smoke" >"$tmp/log" 2>&1 &
app=$!

i=0; while ! grep -q '^JOIN #smoke' "$tmp/server.log" 2>/dev/null && [ $i -lt 150 ]; do sleep 0.1; i=$((i+1)); done
i=0; while [ ! -s "$tmp/shot.png" ] && [ $i -lt 150 ]; do sleep 0.1; i=$((i+1)); done
sleep 1

fail=0
kill -0 "$app" 2>/dev/null || { echo "FAIL: client exited"; tail -20 "$tmp/log"; fail=1; }
grep -q '^NICK smoke' "$tmp/server.log" || { echo "FAIL: no NICK"; fail=1; }
grep -q '^JOIN #smoke' "$tmp/server.log" || { echo "FAIL: no JOIN"; fail=1; }
[ -s "$tmp/shot.png" ] || { echo "FAIL: no window shot"; fail=1; }
if grep -qE 'CRITICAL' "$tmp/log"; then echo "FAIL: criticals"; grep CRITICAL "$tmp/log" | head; fail=1; fi
[ $fail -eq 0 ] && echo "smoke-gui: ok" || exit 1

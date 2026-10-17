#!/bin/sh
# gui_input_paste_max: pasting more lines than that asks first. Cancel
# sends nothing and puts the text back; OK sends every line.
# usage: tests/ux/paste.sh path/to/nexchat     (needs Xvfb + XTest + xsel)
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
command -v xsel >/dev/null || { echo "SKIP: no xsel"; exit 77; }
disp=${DISP:-:92}
tmp=$(mktemp -d)
app=; ircd=; xv=
trap 'kill $app $ircd $xv 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"' EXIT

start_xvfb "$xvfb" "$disp"
mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = smoke\ngui_slist_skip = 1\ngui_win_left = 0\ngui_win_top = 0\ngui_win_width = 900\ngui_win_height = 600\n' > "$tmp/cfg/hexchat.conf"
python3 "$here/../fake_ircd.py" --timeout 60 --log "$tmp/server.log" --portfile "$tmp/port" 2>/dev/null &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
DISPLAY=$disp "$bin" -a -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/#t" >"$tmp/log" 2>&1 &
app=$!
wait_window "$disp"
wait_for grep -q '^JOIN #t' "$tmp/server.log"
sleep 1

k() { DISPLAY=$disp python3 "$here/xkey.py" --delay 0.4 --at 450,580 "$@"; }
# 8 lines on the clipboard (the default limit is 5)
printf 'line1\nline2\nline3\nline4\nline5\nline6\nline7\nline8' | DISPLAY=$disp xsel -b -i

fail=0
sent() { grep -c '^PRIVMSG #t :line' "$tmp/server.log" || true; }

k ctrl+v Return; sleep 1
[ "$(sent)" -eq 0 ] && echo "ok:   nothing sent before answering" || { echo "FAIL: sent before asking"; fail=1; }
# the question has the focus: Escape is Cancel
DISPLAY=$disp python3 "$here/xkey.py" --delay 0.4 Escape; sleep 1
[ "$(sent)" -eq 0 ] && echo "ok:   Cancel sends nothing" || { echo "FAIL: Cancel sent lines"; fail=1; }
# the text is back in the box: Enter asks again, Enter on OK sends
k Return; sleep 1
DISPLAY=$disp python3 "$here/xkey.py" --delay 0.4 Return; sleep 2
[ "$(sent)" -eq 8 ] && echo "ok:   OK sends all 8 lines" || { echo "FAIL: OK sent $(sent) lines, not 8"; fail=1; }

# a short paste goes straight out
printf 'short1\nshort2' | DISPLAY=$disp xsel -b -i
k ctrl+v Return; sleep 1.5
grep -q '^PRIVMSG #t :short2' "$tmp/server.log" && echo "ok:   2 lines go without asking" || { echo "FAIL: short paste"; fail=1; }

if [ $fail -ne 0 ]; then grep PRIVMSG "$tmp/server.log" || true; exit 1; fi
echo "paste: ok"

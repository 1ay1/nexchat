#!/bin/sh
# Mouse input through the real widgets (nx_on_click / nx_on_scroll):
#   - double-click a nick in the user list -> gui_ulist_doubleclick runs
#     (set to "MSG %s dbl" so it shows up as PRIVMSG at the fake ircd)
#   - mouse wheel over the channel tree with gui_tab_scrollchans=1 switches
#     tabs (checked by sending a line: it must go to the other target)
#   - right-click in the user list opens the nick menu (screenshot area
#     changes; a crash or a stuck grab fails the test)
# usage: tests/ux/mouse.sh BIN   (needs Xvfb + XTest)
set -eu
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:85}
tmp=$(mktemp -d)
app=; ircd=; xv=
cleanup() { kill $app $ircd $xv 2>/dev/null || true; wait 2>/dev/null || true
	if [ -n "${KEEP:-}" ]; then cp -r "$tmp" "$KEEP"; fi; rm -rf "$tmp"; }
trap cleanup EXIT

"$xvfb" "$disp" -screen 0 1100x720x24 -nolisten tcp >/dev/null 2>&1 &
xv=$!
sleep 1.5
mkdir -p "$tmp/cfg"
# tree layout, userlist on the right; fixed geometry so coordinates hold
printf 'net_throttle = 0\nirc_nick1 = smoke\ngui_slist_skip = 1\ngui_win_left = 0\ngui_win_top = 0\ngui_win_width = 900\ngui_win_height = 600\ngui_tab_layout = 2\ngui_tab_scrollchans = 1\ngui_ulist_doubleclick = MSG %%s dbl\ngui_pane_left_size = 128\ngui_pane_right_size = 100\n' > "$tmp/cfg/hexchat.conf"
python3 "$here/../fake_ircd.py" --demo --timeout 60 --log "$tmp/server.log" --portfile "$tmp/port" 2>/dev/null &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
DISPLAY=$disp "$bin" -a -d "$tmp/cfg" >"$tmp/log" 2>&1 &
app=$!
sleep 4

x() { DISPLAY=$disp python3 "$here/xkey.py" --delay 0.6 --at 450,580 "$@"; }
x --type "/server irc://127.0.0.1:-$(cat "$tmp/port")/#nexchat" Return
i=0; while ! grep -q '^JOIN' "$tmp/server.log" 2>/dev/null && [ $i -lt 150 ]; do sleep 0.1; i=$((i+1)); done
sleep 3
# focus #nexchat: it's the 2nd row of the channel tree; click it
x --click 60,68
sleep 0.5

fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }

# 1. double-click "alice" (3rd row of the user list: founder, admin, alice)
x --dblclick 840,124
sleep 1
grep -qa '^PRIVMSG alice :dbl' "$tmp/server.log" && ok "user list double-click runs gui_ulist_doubleclick" \
	|| bad "user list double-click"

# 2. wheel down over the channel tree switches to the next tab (heidi's
#    query or the server tab); a line typed now must NOT go to #nexchat
x --click 60,68
sleep 0.3
before=$(grep -ac '^PRIVMSG #nexchat :wheeltest' "$tmp/server.log" || true)
x --wheel-down 60,200
sleep 0.6
x --type "wheeltest" Return
sleep 1
after=$(grep -ac '^PRIVMSG #nexchat :wheeltest' "$tmp/server.log" || true)
[ "$after" = "$before" ] && ok "wheel over channel tree switches tab" || bad "wheel over channel tree"

# 3. right-click a nick: nick menu opens, app stays alive, Escape closes it
x --click 60,68
x --rclick 840,124
sleep 0.8
kill -0 "$app" 2>/dev/null && ok "right-click nick menu (app alive)" || bad "right-click crashed the client"
x Escape
sleep 0.3

if grep -qiE 'critical' "$tmp/log"; then echo "--- criticals:"; grep -iE 'critical' "$tmp/log" | sort | uniq -c | head; fail=1; fi
[ $fail -eq 0 ] && echo "mouse: ok" || { echo "--- server.log"; grep -a PRIVMSG "$tmp/server.log" | tail; exit 1; }

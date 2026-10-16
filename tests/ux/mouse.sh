#!/bin/sh
# Mouse input through the real widgets (nx_on_click / nx_on_scroll):
#   - double-click a nick in the user list -> gui_ulist_doubleclick runs
#     (set to "MSG %s dbl" so it shows up as PRIVMSG at the fake ircd)
#   - mouse wheel over the channel tree with gui_tab_scrollchans=1 switches
#     tabs (checked by sending a line: it must go to the other target)
#   - right-click in the user list opens the nick menu (screenshot area
#     changes; a crash or a stuck grab fails the test)
#   - chat view (xtext): drag-select and double-click-word copy the text
#     (text_autocopy_text=1); pasted and sent, it shows up at the ircd
#   - layout drag and drop: user list onto the left pane, channel tree onto
#     the chat scrollbar; checked in the saved gui_ulist_pos / gui_tab_pos
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
printf 'net_throttle = 0\nirc_nick1 = smoke\ngui_slist_skip = 1\ngui_win_left = 0\ngui_win_top = 0\ngui_win_width = 900\ngui_win_height = 600\ngui_tab_layout = 2\ngui_tab_scrollchans = 1\ngui_ulist_doubleclick = MSG %%s dbl\ngui_pane_left_size = 128\ngui_pane_right_size = 100\ntext_autocopy_text = 1\n' > "$tmp/cfg/hexchat.conf"
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

# 4. drag across "morning everyone" in the chat view: with
#    text_autocopy_text=1 the release copies it; paste it and send it
x --click 60,68
x --drag 331,180:500,180
sleep 0.5
x ctrl+v Return
sleep 1
grep -qa '^PRIVMSG #nexchat :morning everyone' "$tmp/server.log" && ok "chat view drag-select copies" \
	|| bad "chat view drag-select"

# 5. double-click a word in the chat view selects just that word
x --dblclick 395,201
sleep 0.5
x ctrl+v Return
sleep 1
grep -qa '^PRIVMSG #nexchat :smoke,\{0,1\}$' "$tmp/server.log" && ok "chat view double-click selects a word" \
	|| bad "chat view double-click word"

# 6. drag the user list onto the bottom half of the left pane (under the
#    channel tree): gui_ulist_pos becomes 2 (left-bottom); then the channel
#    tree back onto the right: the layout DnD round trip
x --drag 840,380:60,450
sleep 1.5
# /set writes hexchat.conf, with the positions as they are now
x --click 450,580
x --type "/set -quiet gui_tab_small 0" Return
sleep 1
grep -q '^gui_ulist_pos = 2$' "$tmp/cfg/hexchat.conf" && ok "drag user list to left-bottom" \
	|| bad "drag user list: $(grep '^gui_ulist_pos' "$tmp/cfg/hexchat.conf")"
# the tree onto the chat scrollbar's bottom half (a drop target on the
# right side): it goes right-bottom
x --drag 60,68:890,450
sleep 1.5
x --click 450,580
x --type "/set -quiet gui_tab_small 0" Return
sleep 1
grep -q '^gui_tab_pos = 4$' "$tmp/cfg/hexchat.conf" && ok "drag channel tree to right-bottom" \
	|| bad "drag channel tree: $(grep '^gui_tab_pos' "$tmp/cfg/hexchat.conf")"

# 7. right-click a URL in the chat view: the URL menu; "Copy Selected Link"
#    (Open, then Copy: Down Down) puts it on the clipboard; paste and send
x --rclick 470,264
sleep 0.8
x Down Down Return
sleep 0.5
x --click 450,580
x ctrl+v Return
sleep 1
grep -qa '^PRIVMSG #nexchat :https://hexchat.github.io$' "$tmp/server.log" && ok "chat view URL menu copies the link" \
	|| bad "chat view URL menu copy"

# 8. middle-click a tab closes it (gui_tab_middleclose=1): heidi's query
#    goes, so the line we send next goes to #nexchat
x --mclick 870,94
sleep 1
x --click 450,580
x --type "mclicktest" Return
sleep 1
grep -qa '^PRIVMSG #nexchat :mclicktest' "$tmp/server.log" && ok "middle-click closes a tab" \
	|| bad "middle-click tab close"

if [ -n "${KEEP:-}" ]; then /opt/X11/bin/xwd -root -display "$disp" > "$tmp/end.xwd" 2>/dev/null || xwd -root -display "$disp" > "$tmp/end.xwd" || true; fi
if grep -qiE 'critical' "$tmp/log"; then echo "--- criticals:"; grep -iE 'critical' "$tmp/log" | sort | uniq -c | head; fail=1; fi
[ $fail -eq 0 ] && echo "mouse: ok" || { echo "--- server.log"; grep -a PRIVMSG "$tmp/server.log" | tail; exit 1; }

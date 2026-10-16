#!/bin/sh
# Window manager hints on X11: the main window opens where gui_win_left/top
# say, a move is saved, and /GUI FLASH sets the urgency hint while another
# window has focus, cleared once ours is active again. The focus part runs
# under openbox (GTK4 learns about focus from the WM).
# usage: tests/ux/wm.sh BIN     (needs Xvfb, xprop, xwininfo, xdotool)
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:79}
command -v xprop >/dev/null && command -v xwininfo >/dev/null && command -v xdotool >/dev/null ||
	{ echo "wm: no xprop/xwininfo/xdotool, skipped"; exit 77; }
tmp=$(mktemp -d)
app=; xv=; wm=; thief=
cleanup() { kill $app $wm $thief $xv 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT
start_xvfb "$xvfb" "$disp"
mkdir -p "$tmp/cfg"
printf 'gui_slist_skip = 1\nirc_nick1 = wm\ngui_win_left = 123\ngui_win_top = 77\ngui_win_width = 700\ngui_win_height = 500\ngui_win_save = 1\n' > "$tmp/cfg/hexchat.conf"

fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }

DISPLAY=$disp "$bin" -a -d "$tmp/cfg" >"$tmp/log" 2>&1 &
app=$!
wait_window "$disp"

# our top-level (needs xdotool)
win=$(DISPLAY=$disp xdotool search --onlyvisible --class nexchat 2>/dev/null | tail -1)
win=$(printf '0x%x' "${win:-0}")
[ "$win" != 0x0 ] || { echo "FAIL: no window"; exit 1; }

geo=$(DISPLAY=$disp xwininfo -id "$win")
x=$(echo "$geo" | sed -n 's/.*Absolute upper-left X: *//p')
y=$(echo "$geo" | sed -n 's/.*Absolute upper-left Y: *//p')
[ "$x" = 123 ] && [ "$y" = 77 ] && ok "opens at gui_win_left/top (123,77)" || bad "opens at $x,$y, not 123,77"

# move the window (as a WM would); /set writes hexchat.conf with the
# position as it is now
DISPLAY=$disp xdotool windowmove "$win" 210 140
sleep 1
DISPLAY=$disp python3 "$here/xkey.py" --delay 0.4 --at 300,300 --type "/set -quiet gui_tab_small 0" Return
sleep 1
sx=$(sed -n 's/^gui_win_left = //p' "$tmp/cfg/hexchat.conf")
sy=$(sed -n 's/^gui_win_top = //p' "$tmp/cfg/hexchat.conf")
[ "$sx" = 210 ] && [ "$sy" = 140 ] && ok "moved position is saved (210,140)" || bad "saved $sx,$sy, not 210,140"

# urgency: needs a WM that tracks the active window
ob=${OPENBOX:-$(command -v openbox || true)}
if [ -z "$ob" ]; then
	echo "wm: no openbox, urgency checks skipped"
else
	DISPLAY=$disp "$ob" --sm-disable >/dev/null 2>&1 &
	wm=$!
	i=0; while ! DISPLAY=$disp xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q window && [ $i -lt 50 ]; do
		sleep 0.1; i=$((i+1)); done
	DISPLAY=$disp xdotool windowactivate --sync "$win" 2>/dev/null || true
	sleep 0.5
	urgent() { DISPLAY=$disp xprop -id "$win" WM_HINTS 2>/dev/null | grep -qi urgency; }
	urgent && bad "urgent before /gui flash" || true
	# /gui flash does nothing to a focused window: a /timer fires it after
	# another window took focus
	DISPLAY=$disp xdotool type --delay 40 "/timer 2 gui flash"
	DISPLAY=$disp xdotool key Return
	DISPLAY=$disp python3 "$here/xkey.py" --steal-focus 15 &
	thief=$!
	sleep 1
	DISPLAY=$disp xdotool windowactivate --sync "$(DISPLAY=$disp xdotool search --name nx-focus-thief | tail -1)" 2>/dev/null || true
	sleep 3
	urgent && ok "/gui flash sets the urgency hint" || bad "no urgency hint after /gui flash"
	DISPLAY=$disp xdotool windowactivate --sync "$win" 2>/dev/null || true
	sleep 1
	urgent && bad "urgency hint stays after focus" || ok "activating the window clears it"
fi

if grep -q CRITICAL "$tmp/log"; then echo "FAIL: criticals"; grep CRITICAL "$tmp/log" | head; fail=1; fi
[ $fail -eq 0 ] && echo "wm: ok" || exit 1

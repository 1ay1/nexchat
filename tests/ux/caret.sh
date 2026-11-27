#!/bin/sh
# The input box's block cursor, as drawn (X11, focused by hand; the app
# shoots itself): a filled box exactly over the character under the cursor
# with that character in the background colour; a whole emoji with its
# skin tone (one grapheme) covered, not half of it; a digit-wide box past
# the end; a hollow box when the window loses focus. In a light and a dark
# scheme.
# usage: tests/ux/caret.sh path/to/nexchat
set -eu
# a private session bus: the test windows' tray icons must not land on
# the desktop's panel
if [ -z "${NX_IN_BUS:-}" ] && command -v dbus-run-session >/dev/null; then
	busdir=$(mktemp -d)
	printf '<busconfig><type>session</type><listen>unix:dir=%s</listen><policy context="default"><allow send_destination="*" eavesdrop="true"/><allow eavesdrop="true"/><allow own="*"/></policy></busconfig>\n' \
		"$busdir" > "$busdir/bus.conf"
	NX_IN_BUS=1 dbus-run-session --config-file="$busdir/bus.conf" -- sh "$0" "$@"
	st=$?; rm -rf "$busdir"; exit $st
fi
. "$(dirname "$0")/isolate.sh"
bin=$1
here=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
# every process started, one pid per line: the halves run as background
# subshells, so a shell variable set in them never reaches this trap
trap 'kill $(cat "$tmp"/*/pids 2>/dev/null) 2>/dev/null || true; [ -n "${KEEP:-}" ] && cp -r "$tmp" "$KEEP"; rm -rf "$tmp"' EXIT

one() # DISP SCHEME OUTDIR
{
	d=$1; scheme=$2; out=$3
	mkdir -p "$out/cfg"
	start_xvfb "$(command -v Xvfb)" "$d" 1000 600
	echo $xv >> "$out/pids"
	python3 "$here/tests/fake_ircd.py" --timeout 40 --log "$out/s.log" --portfile "$out/port" >/dev/null 2>&1 &
	echo $! >> "$out/pids"
	i=0; while [ ! -s "$out/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
	printf 'gui_slist_skip = 1\nirc_nick1 = yo\ngui_win_width = 700\ngui_win_height = 300\ngui_input_cursor = 2\ngui_input_blink = 0\n' > "$out/cfg/hexchat.conf"
	[ -n "$scheme" ] && printf 'scheme = %s\n' "$scheme" > "$out/cfg/colors.conf"
	DISPLAY=$d NEXCHAT_SHOT_ON="$out/shoot" \
	NEXCHAT_XDRAG="wait 60;text hello wörld 👍🏽 ok;lay 1;wait 30;caret 6;lay 2;wait 30;caret 12;lay 3;wait 30;caret 0;lay 4;wait 200" \
	NEXCHAT_XCHECK="$out/where" \
		"$bin" -n -d "$out/cfg" "irc://127.0.0.1:-$(cat "$out/port")/#c" >"$out/log" 2>&1 &
	app=$!; echo $app >> "$out/pids"
	i=0; while ! DISPLAY=$d xdotool search --pid $app --onlyvisible >/dev/null 2>&1 && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
	DISPLAY=$d xdotool windowfocus --sync "$(DISPLAY=$d xdotool search --pid $app --onlyvisible | head -1)" 2>/dev/null || true
	# ask for a shot and wait for the picture itself (the app removes the
	# trigger before it shoots, so that says nothing about the PNG)
	snap() { echo "$out/$1.png" > "$out/shoot"
		i=0; while [ ! -s "$out/$1.png" ] && [ $i -lt 200 ]; do sleep 0.05; i=$((i+1)); done
		sleep 0.1; }
	shoot() { i=0; while ! grep -q "^xlay: $1 " "$out/log" && [ $i -lt 150 ]; do sleep 0.05; i=$((i+1)); done
		sleep 0.25; snap "$2"; }
	shoot 1 end; shoot 2 mid; shoot 3 emoji; shoot 4 start
	DISPLAY=$d xdotool windowfocus 0 2>/dev/null || true
	sleep 0.4; snap inactive
	# done with this half: nothing of it outlives the test
	kill $(cat "$out/pids") 2>/dev/null || true
}

one :64 "" "$tmp/light" &
w1=$!
one :65 Dracula "$tmp/dark" &
w2=$!
wait $w1 $w2

python3 "$(dirname "$0")/caret-check.py" "$tmp/light" light "$tmp/dark" dark
st=$?
n=$(cat "$tmp"/*/log | grep -c CRITICAL || true)
[ "$n" -eq 0 ] || { echo "FAIL: $n criticals"; st=1; }
[ $st -eq 0 ] && echo "caret: ok"
exit $st

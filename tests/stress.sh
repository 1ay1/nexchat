#!/bin/sh
# Stress: tests/flood_ircd.py floods the client (messages, joins/parts,
# nick changes, modes, kicks, queries, the client kicked and rejoined)
# while we hammer the UI: wheel scrolls, clicks, right-click menus,
# selection, tab switching, zoom, clear, typing. Passes when the client is
# alive and still answers afterwards (it must process /quit and exit by
# itself) and logged no CRITICAL or crash.
# usage: tests/stress.sh BIN [SECONDS]   (needs Xvfb + XTest)
#        SHOT=file.png saves the window at the end
set -eu
here=$(cd "$(dirname "$0")" && pwd)
NX_UX_DIR=$here/ux
. "$here/ux/isolate.sh"
bin=$1
secs=${2:-20}
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
xwd=${XWD:-$(command -v xwd || echo /opt/X11/bin/xwd)}
disp=${DISP:-:91}
tmp=$(mktemp -d)
app=; ircd=; xv=
cleanup() { kill $app $ircd $xv 2>/dev/null || true; wait 2>/dev/null || true
	if [ -n "${KEEP:-}" ]; then cp -r "$tmp" "$KEEP"; fi; rm -rf "$tmp"; }
trap cleanup EXIT

start_xvfb "$xvfb" "$disp"
mkdir -p "$tmp/cfg"
# small scrollback so lines get dropped from the top all the time too;
# tree layout, fixed geometry for the click coordinates
printf 'net_throttle = 0\nirc_nick1 = stress\ngui_slist_skip = 1\ngui_win_left = 0\ngui_win_top = 0\ngui_win_width = 900\ngui_win_height = 600\ngui_tab_layout = 2\ntext_max_lines = 500\ngui_join_dialog = 0\ngui_quit_dialog = 0\ntext_autocopy_text = 1\nirc_conf_mode = 1\n' > "$tmp/cfg/hexchat.conf"
python3 "$here/flood_ircd.py" --seconds "$secs" --seed "${SEED:-1}" --timeout 60 \
	--log "$tmp/sent" --portfile "$tmp/port" 2>"$tmp/ircd.err" &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
DISPLAY=$disp ${WRAP:-} "$bin" -a -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")" >"$tmp/log" 2>&1 &
app=$!
wait_window "$disp"

x() { DISPLAY=$disp python3 "$here/ux/xkey.py" --delay 0.05 --at 450,580 "$@" 2>/dev/null || true; }

# random UI actions until the flood is over. Coordinates: channel tree at
# x 60, chat 200-700 x 80-520, user list x 840, input box y 580.
end=$(( $(date +%s) + secs ))
n=0
while [ "$(date +%s)" -lt "$end" ]; do
	kill -0 "$app" 2>/dev/null || break
	n=$((n + 1))
	r=$(awk -v s="$n" 'BEGIN { srand(s * 7919); print int(rand() * 14) }')
	y=$(awk -v s="$n" 'BEGIN { srand(s * 104729); print 80 + int(rand() * 440) }')
	case $r in
	0|1) x --wheel-up 400,$y --wheel-up 400,$y --wheel-up 400,$y ;;
	2) x --wheel-down 400,$y --wheel-down 400,$y ;;
	3) x --click 60,$(( 40 + (y % 8) * 20 )) ;;		# switch tabs in the tree
	4) x --rclick 400,$y Escape ;;					# text menu
	5) x --rclick 840,$y Escape ;;					# nick menu
	6) x --dblclick 400,$y ;;						# word select
	7) x --drag 300,$y:600,$(( y + 60 )) ;;			# drag select
	8) x ctrl+equal ;;
	9) x ctrl+minus ;;
	10) x --click 650,500 ;;						# where the jump button sits
	11) x --type "hello $n" Return ;;
	12) x ctrl+Page_Down ;;
	13) x --type "/clear" Return ;;
	esac
done

alive=1
kill -0 "$app" 2>/dev/null || alive=0
fail=0
if [ $alive = 1 ]; then
	echo "ok:   alive after $n UI actions under $(cat "$tmp/sent" 2>/dev/null || echo '?') lines flood"
	if [ -n "${SHOT:-}" ]; then
		DISPLAY=$disp "$xwd" -root -silent -out "$tmp/s.xwd" && python3 "$here/ux/xwd2png.py" "$tmp/s.xwd" "$SHOT"
	fi
	# still responsive: it must handle input and exit on its own (Ctrl+Q is
	# nexchat > Quit; /quit only disconnects). Show the menu bar first, the
	# shortcut works either way.
	x Escape Escape
	x ctrl+0
	x ctrl+q
	# a slow (sanitizer) build may still be chewing through the backlog
	limit=$(( ${EXIT_WAIT:-15} * 10 ))
	i=0; while kill -0 "$app" 2>/dev/null && [ $i -lt $limit ]; do sleep 0.1; i=$((i+1)); done
	if kill -0 "$app" 2>/dev/null; then
		echo "FAIL: didn't exit within $((limit / 10))s of Ctrl+Q (hung?)"; fail=1
	else
		echo "ok:   exited $((i / 10)).$((i % 10))s after Ctrl+Q"
		wait "$app" 2>/dev/null && st=0 || st=$?
		[ "$st" = 0 ] && echo "ok:   quit cleanly" || { echo "FAIL: exit status $st"; fail=1; }
	fi
else
	wait "$app" 2>/dev/null && st=0 || st=$?
	echo "FAIL: client died (exit $st)"; fail=1
fi
app=

if grep -qE 'CRITICAL|ERROR \*\*|AddressSanitizer|runtime error|Segmentation' "$tmp/log"; then
	echo "FAIL: problems in the log:"
	grep -E 'CRITICAL|ERROR \*\*|AddressSanitizer|runtime error|Segmentation|WARNING' "$tmp/log" | sort | uniq -c | sort -rn | head -20
	fail=1
fi
if grep -q WARNING "$tmp/log"; then
	echo "note: warnings:"
	grep WARNING "$tmp/log" | grep -v libenchant | sort | uniq -c | sort -rn | head -10
fi
exit $fail

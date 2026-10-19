#!/bin/sh
# The channel switcher as tabs (gui_tab_layout = 0) on GTK4: no GTK3
# signals left (no GLib criticals at all), a click on a tab switches to
# it, a click on the front tab keeps it, and with more tabs than fit the
# window keeps its width and the scroll arrows show.
# usage: tests/ux/tabs.sh path/to/nexchat
set -eu
bin=$1
here=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
srv=; app=
trap 'kill $app $srv 2>/dev/null; rm -rf "$tmp"' EXIT
export GTK_A11Y=none

python3 "$here/tests/fake_ircd.py" --timeout 60 --log "$tmp/server.log" --portfile "$tmp/port" &
srv=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
mkdir "$tmp/cfg"
printf 'gui_slist_skip = 1\ngui_tab_layout = 0\ngui_tab_pos = %s\nirc_nick1 = tabber\nnet_throttle = 0\ngui_win_width = 700\n' "${TABPOS:-1}" > "$tmp/cfg/hexchat.conf"
chans="#one,#two,#three,#four,#five,#six,#seven,#eight,#nine,#ten,#eleven,#twelve,#thirteen,#fourteen,#fifteen,#sixteen"
NEXCHAT_TABS_DUMP=1 NEXCHAT_TAB_CLICK="$tmp/click" NEXCHAT_WINDOWS_DUMP=1 NEXCHAT_SHOT="$tmp/shot.png:12" \
	"$bin" -n -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/$chans" >"$tmp/log" 2>&1 &
app=$!

fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }
front() { grep '^front:' "$tmp/log" | tail -1 | cut -c8-; }
click() { printf '%s\n' "$1" > "$tmp/click"; i=0; while [ -f "$tmp/click" ] && [ $i -lt 30 ]; do sleep 0.2; i=$((i+1)); done; sleep 1.2; }

i=0; while ! grep -q '^tab: #sixteen$' "$tmp/log" && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done
grep -q '^tab: #sixteen$' "$tmp/log" && ok "16 channel tabs" || bad "tabs didn't all open"

click "#three"
[ "$(front)" = "#three" ] && ok "click a tab: it's in front" || bad "click #three: front is $(front)"
click "#three"
[ "$(front)" = "#three" ] && ok "click the front tab: it stays" || bad "click front tab again: front is $(front)"
click "#twelve"
[ "$(front)" = "#twelve" ] && ok "click another: switches" || bad "click #twelve: front is $(front)"

i=0; while ! grep -q '^window 0:' "$tmp/log" && [ $i -lt 20 ]; do sleep 0.5; i=$((i+1)); done
w=$(grep '^window 0:' "$tmp/log" | tail -1 | sed -n 's/.* \([0-9][0-9]*\)x[0-9][0-9]* child=.*/\1/p')
[ -n "$w" ] && [ "$w" -le 800 ] && ok "window stays narrow with many tabs (${w}px)" || bad "window grew to ${w}px"

n=$(grep -c 'CRITICAL' "$tmp/log" || true)
[ "$n" -eq 0 ] && ok "no GLib/GTK criticals" || { bad "$n criticals"; grep CRITICAL "$tmp/log" | sort | uniq -c | head; }

[ -n "${KEEP:-}" ] && [ -f "$tmp/shot.png" ] && cp "$tmp/shot.png" "$KEEP/tabs.png"
[ $fail -eq 0 ] && echo "tabs: ok"
exit $fail

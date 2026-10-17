#!/bin/sh
# gui_tab_unread: "#chan (n)" on a tab with unread messages, "(n, h!)" when
# some mention you; the tab in front has none, and looking at a tab clears it.
# usage: tests/ux/unread.sh path/to/nexchat     (needs Xvfb + XTest)
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:94}
tmp=$(mktemp -d)
app=; ircd=; xv=
trap 'kill $app $ircd $xv 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"' EXIT

# once #bravo (joined last) is in front: three lines in #alpha, one naming us
cat > "$tmp/script" <<'EOF'
:bob!b@h PRIVMSG #alpha :just chatting
:bob!b@h PRIVMSG #alpha :hey {nick}, look
:bob!b@h PRIVMSG #alpha :third line
EOF
start_xvfb "$xvfb" "$disp"
mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = smoke\ngui_slist_skip = 1\ngui_win_left = 0\ngui_win_top = 0\ngui_win_width = 900\ngui_win_height = 600\n' > "$tmp/cfg/hexchat.conf"
python3 "$here/../fake_ircd.py" --timeout 60 --log "$tmp/server.log" --portfile "$tmp/port" \
	--script "$tmp/script" --script-after '#bravo' 2>/dev/null &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
DISPLAY=$disp NEXCHAT_TABS_DUMP=1 \
	"$bin" -a -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/#alpha,#bravo" >"$tmp/log" 2>&1 &
app=$!
wait_window "$disp"
wait_for grep -q '^tab: #bravo' "$tmp/log"
sleep 2

# the tabs as of the last full dump
last() { awk '/^tabs:$/{n++} {r[n]=r[n] $0 "\n"} END{printf "%s", r[n-1]}' "$tmp/log"; }
fail=0
expect() {
	if last | grep -qx -- "tab: $1"; then echo "ok:   $2"; else echo "FAIL: $2"; last; fail=1; fi
}
expect '#alpha (3, 1!)' 'background tab counts 3 lines, 1 highlight'
expect '#bravo' 'the tab in front has no count'

# Ctrl+PageUp: to #alpha, which clears its count
DISPLAY=$disp python3 "$here/xkey.py" --delay 0.4 --at 450,580 ctrl+Page_Up
sleep 2.5
expect '#alpha' 'looking at #alpha clears its count'

[ $fail -eq 0 ] || exit 1
echo "unread: ok"

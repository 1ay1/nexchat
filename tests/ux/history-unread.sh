#!/bin/sh
# History from the server (chathistory) in a background tab isn't counted
# as unread, even with a mention of us in it.
# usage: tests/ux/history-unread.sh path/to/nexchat     (needs Xvfb)
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:88}
tmp=$(mktemp -d)
app=; ircd=; xv=
trap 'kill $app $ircd $xv 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"' EXIT

start_xvfb "$xvfb" "$disp"
mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = hist\ngui_slist_skip = 1\n' > "$tmp/cfg/hexchat.conf"
python3 "$here/../fake_ircd.py" --timeout 60 --log "$tmp/server.log" --portfile "$tmp/port" \
	--caps "batch server-time draft/chathistory" 2>/dev/null &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
# #alpha's history arrives while #bravo (joined last) is in front
DISPLAY=$disp NEXCHAT_TABS_DUMP=1 \
	"$bin" -a -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/#alpha,#bravo" >"$tmp/log" 2>&1 &
app=$!
wait_window "$disp"
wait_for grep -q '^CHATHISTORY LATEST #alpha' "$tmp/server.log"
sleep 2.5

last() { awk '/^tabs:$/{n++} {r[n]=r[n] $0 "\n"} END{printf "%s", r[n-1]}' "$tmp/log"; }
if last | grep -qx 'tab: #alpha'; then
	echo "ok:   history in a background tab: no unread count"
else
	echo "FAIL: history counted"; last | grep '^tab:'; exit 1
fi
echo "history-unread: ok"

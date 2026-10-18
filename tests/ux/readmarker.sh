#!/bin/sh
# IRCv3 draft/read-marker: a channel read on another device (the server
# sends MARKREAD) stops counting as unread here; looking at a tab tells the
# server (MARKREAD back), so the other devices clear it too.
# usage: tests/ux/readmarker.sh path/to/nexchat     (needs Xvfb + XTest)
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:97}
tmp=$(mktemp -d)
app=; ircd=; xv=
trap 'kill $app $ircd $xv 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"' EXIT

# once #bravo is in front: two lines in #alpha (unread), then #charlie
# gets two lines and is marked read elsewhere, after them
cat > "$tmp/script" <<'EOF'
@time=2026-03-01T10:00:00.000Z :bob!b@h PRIVMSG #alpha :one
@time=2026-03-01T10:00:01.000Z :bob!b@h PRIVMSG #alpha :two
@time=2026-03-01T10:00:02.000Z :bob!b@h PRIVMSG #charlie :three
@time=2026-03-01T10:00:03.000Z :bob!b@h PRIVMSG #charlie :four
:fake MARKREAD #charlie timestamp=2026-03-01T10:00:03.000Z
EOF
start_xvfb "$xvfb" "$disp"
mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = rm\ngui_slist_skip = 1\ngui_win_left = 0\ngui_win_top = 0\ngui_win_width = 900\ngui_win_height = 600\n' > "$tmp/cfg/hexchat.conf"
python3 "$here/../fake_ircd.py" --timeout 60 --log "$tmp/server.log" --portfile "$tmp/port" \
	--caps "server-time draft/read-marker" --script "$tmp/script" --script-after '#bravo' 2>/dev/null &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
DISPLAY=$disp NEXCHAT_TABS_DUMP=1 \
	"$bin" -a -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/#alpha,#charlie,#bravo" >"$tmp/log" 2>&1 &
app=$!
wait_window "$disp"
wait_for grep -q '^tab: #bravo' "$tmp/log"
sleep 2.5

last() { awk '/^tabs:$/{n++} {r[n]=r[n] $0 "\n"} END{printf "%s", r[n-1]}' "$tmp/log"; }
fail=0
expect() { if last | grep -qx -- "tab: $1"; then echo "ok:   $2"; else echo "FAIL: $2"; last | grep '^tab:'; fail=1; fi; }
grep -q '^CAP REQ .*draft/read-marker' "$tmp/server.log" && echo "ok:   read-marker requested" || { echo "FAIL: not requested"; fail=1; }
expect '#alpha (2)' 'unread lines count as usual'
expect '#charlie' 'read on another device: no count here'

# look at #alpha (Ctrl+PageUp twice from #bravo: #charlie, #alpha)
DISPLAY=$disp python3 "$here/xkey.py" --delay 0.4 --at 450,580 ctrl+Page_Up ctrl+Page_Up
sleep 1.5
if grep -q '^MARKREAD #alpha timestamp=2026-03-01T10:00:01' "$tmp/server.log"; then
	echo "ok:   looking at #alpha tells the server, up to its newest line"
else
	echo "FAIL: no MARKREAD for #alpha"; grep MARKREAD "$tmp/server.log" || true; fail=1
fi
[ $fail -eq 0 ] || exit 1
echo "readmarker: ok"

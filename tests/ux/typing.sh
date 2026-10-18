#!/bin/sh
# +typing (behind gui_typing_show / gui_typing_send): "bob is typing..."
# under the input row while bob types, gone when his line arrives; and
# typing in the box sends +typing=active, sending the line +typing=done.
# usage: tests/ux/typing.sh path/to/nexchat     (needs Xvfb + XTest)
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:98}
tmp=$(mktemp -d)
app=; ircd=; xv=
trap 'kill $app $ircd $xv 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"' EXIT

start_xvfb "$xvfb" "$disp"
mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = ty\ngui_slist_skip = 1\ngui_win_left = 0\ngui_win_top = 0\ngui_win_width = 900\ngui_win_height = 600\ngui_typing_show = 1\ngui_typing_send = 1\n' > "$tmp/cfg/hexchat.conf"
cat > "$tmp/script" <<'EOF'
@+typing=active :bob!b@h TAGMSG #t
EOF
python3 "$here/../fake_ircd.py" --timeout 60 --log "$tmp/server.log" --portfile "$tmp/port" \
	--caps "message-tags" --script "$tmp/script" 2>/dev/null &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
DISPLAY=$disp NEXCHAT_TABS_DUMP=1 \
	"$bin" -a -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/#t" >"$tmp/log" 2>&1 &
app=$!
wait_window "$disp"
wait_for grep -q '^tab: #t' "$tmp/log"
sleep 1.5

last() { awk '/^tabs:$/{n++} {r[n]=r[n] $0 "\n"} END{printf "%s", r[n-1]}' "$tmp/log"; }
fail=0
if last | grep -qx 'typing: bob is typing...'; then echo "ok:   bob is typing... shows"; else echo "FAIL: no typing line"; last | grep typing; fail=1; fi

DISPLAY=$disp python3 "$here/xkey.py" --delay 0.3 --at 450,580 --type "hello there"
sleep 1
grep -q '^@+typing=active TAGMSG #t' "$tmp/server.log" && echo "ok:   typing sends +typing=active" || { echo "FAIL: no +typing=active"; fail=1; }
DISPLAY=$disp python3 "$here/xkey.py" --delay 0.3 --at 450,580 Return
sleep 1
grep -q '^@+typing=done TAGMSG #t' "$tmp/server.log" && echo "ok:   sending the line sends +typing=done" || { echo "FAIL: no +typing=done"; fail=1; }
active=$(grep -c '^@+typing=active' "$tmp/server.log" || true)
[ "$active" -le 2 ] && echo "ok:   not one per key ($active for 11 keys)" || { echo "FAIL: $active +typing=active"; fail=1; }

# bob goes stale after 6 s without another active
sleep 6
if last | grep -qx 'typing: '; then echo "ok:   gone after 6 s"; else echo "FAIL: typing line stays"; last | grep typing; fail=1; fi
[ $fail -eq 0 ] || { grep -i typing "$tmp/server.log"; exit 1; }
echo "typing: ok"

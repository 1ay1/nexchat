#!/bin/sh
# Hovering a link in the chat shows where it really goes as a tooltip,
# the whole URL even when the line has more text around it.
# usage: tests/ux/urltip.sh path/to/nexchat     (needs Xvfb + XTest)
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:91}
tmp=$(mktemp -d)
app=; ircd=; xv=
trap 'kill $app $ircd $xv 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"' EXIT

cat > "$tmp/script" <<'EOF'
:bob!b@h PRIVMSG #t :see https://example.org/a/very/long/path?q=1 for more
EOF
start_xvfb "$xvfb" "$disp"
mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = smoke\ngui_slist_skip = 1\ngui_win_left = 0\ngui_win_top = 0\ngui_win_width = 900\ngui_win_height = 600\ngui_ulist_hide = 1\ngui_tab_pos = 6\ntext_font = DejaVu Sans Mono 10\nstamp_text = 0\n' > "$tmp/cfg/hexchat.conf"
python3 "$here/../fake_ircd.py" --timeout 60 --log "$tmp/server.log" --portfile "$tmp/port" --script "$tmp/script" 2>/dev/null &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
DISPLAY=$disp NEXCHAT_TABS_DUMP=1 \
	"$bin" -a -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/#t" >"$tmp/log" 2>&1 &
app=$!
wait_window "$disp"
wait_for grep -q '^tab: #t' "$tmp/log"
sleep 1

# sweep the pointer along the chat rows until it lands on the link
want='https://example.org/a/very/long/path?q=1'
found=
for y in 40 55 70 85 100 115 130 145 160 175 190; do
	for x in 200 260 320 380; do
		DISPLAY=$disp python3 "$here/xkey.py" --delay 0.05 --at "$x,$y"
		sleep 1.2
		if grep -qxF "tooltip: $want" "$tmp/log"; then found=1; break 2; fi
	done
done
if [ -z "$found" ]; then
	echo "FAIL: no tooltip with the URL"; grep '^tooltip:' "$tmp/log" | sort | uniq -c
	exit 1
fi
echo "ok:   hovering the link shows $want"
# off the link: the tooltip goes away
DISPLAY=$disp python3 "$here/xkey.py" --delay 0.05 --at 450,500
sleep 1.5
awk '/^tooltip:/{t=$0} END{exit t != "tooltip: "}' "$tmp/log" && echo "ok:   off the link, no tooltip" || { echo "FAIL: tooltip stays"; exit 1; }
echo "urltip: ok"

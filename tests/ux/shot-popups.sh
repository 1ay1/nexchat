#!/bin/sh
# Screenshot every right-click popup menu, for before/after comparison of
# menu code: nick (user list + chat view, with the info submenu open), URL,
# channel name, tab, input box (colour submenu), channel list.
# usage: tests/ux/shot-popups.sh BIN OUTDIR   (needs Xvfb + XTest + xwd)
set -eu
bin=$1; out=$2
here=$(cd "$(dirname "$0")" && pwd)
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
xwd=${XWD:-$(command -v xwd || echo /opt/X11/bin/xwd)}
disp=${DISP:-:88}
tmp=$(mktemp -d)
app=; ircd=; xv=
cleanup() { kill $app $ircd $xv 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT
mkdir -p "$out"

"$xvfb" "$disp" -screen 0 1100x720x24 -nolisten tcp >/dev/null 2>&1 &
xv=$!
sleep 1.5
mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = smoke\ngui_slist_skip = 1\ngui_win_left = 0\ngui_win_top = 0\ngui_win_width = 900\ngui_win_height = 600\ngui_tab_layout = 2\ngui_pane_left_size = 128\ngui_pane_right_size = 100\n' > "$tmp/cfg/hexchat.conf"
python3 "$here/../fake_ircd.py" --demo --timeout 90 --log "$tmp/server.log" --portfile "$tmp/port" 2>/dev/null &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
DISPLAY=$disp "$bin" -a -d "$tmp/cfg" >"$tmp/log" 2>&1 &
app=$!
sleep 4

x() { DISPLAY=$disp python3 "$here/xkey.py" --delay 0.6 --at 450,580 "$@"; }
shot() { sleep 1; DISPLAY=$disp "$xwd" -root -silent -out "$tmp/s.xwd"; python3 "$here/xwd2png.py" "$tmp/s.xwd" "$out/$1.png"; }
x --type "/server irc://127.0.0.1:-$(cat "$tmp/port")/#nexchat" Return
i=0; while ! grep -q '^JOIN' "$tmp/server.log" 2>/dev/null && [ $i -lt 150 ]; do sleep 0.1; i=$((i+1)); done
sleep 3
x --click 60,68
sleep 0.5

# nick menu from the user list, info submenu open (first item)
x --rclick 840,124
x Down Right
shot nick-ulist
x Escape Escape Escape
# nick menu from the chat view ("alice" in "<alice> morning everyone")
x --rclick 290,180
shot nick-text
x Escape
# URL menu
x --rclick 470,264
shot url
x Escape
# channel name in the text ("#nexchat" in the first line)
x --rclick 510,75
shot chan
x Escape
# tab menu (channel tree row)
x --rclick 60,68
shot tab
x Escape
# input box: colour submenu
x --rclick 450,580
shot input
x Escape
kill -0 "$app" 2>/dev/null || { echo "app died"; exit 1; }
echo "shots in $out"

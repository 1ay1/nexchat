#!/bin/sh
# Snapshot the main menu tree (labels+mnemonics, accelerators, toggle/radio
# state, sensitivity) at three points and compare with a stored baseline:
#   1. startup, not connected     (Disconnect/Away/Join insensitive)
#   2. connected and joined        (they become sensitive)
#   3. after /MENU adds a submenu with an item, a toggle and a radio pair,
#      updates one, and deletes another (the plugin-facing API)
# usage: tests/ux/menus.sh BIN [--update]   (needs Xvfb + XTest)
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1; update=${2:-}
here=$(cd "$(dirname "$0")" && pwd)
base="$here/menus.expected"
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:95}
tmp=$(mktemp -d)
app=; ircd=; xv=
cleanup() { kill $app $ircd $xv 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT

start_xvfb "$xvfb" "$disp"
mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = smoke\ngui_slist_skip = 1\ngui_win_left = 0\ngui_win_top = 0\ngui_win_width = 900\ngui_win_height = 600\n' > "$tmp/cfg/hexchat.conf"
python3 "$here/../fake_ircd.py" --timeout 60 --log "$tmp/server.log" --portfile "$tmp/port" 2>/dev/null &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done

NEXCHAT_MENU_DUMP="$tmp/dump" DISPLAY=$disp "$bin" -a -n -d "$tmp/cfg" >"$tmp/log" 2>&1 &
app=$!
sleep 4
cp "$tmp/dump" "$tmp/1-startup"

k() { DISPLAY=$disp python3 "$here/xkey.py" --delay 0.5 --at 450,580 "$@"; }
t() { DISPLAY=$disp python3 "$here/xkey.py" --delay 0.5 --at 450,580 --type "$1"; k Return; sleep 0.6; }

t "/server irc://127.0.0.1:-$(cat "$tmp/port")/#m"
i=0; while ! grep -q '^JOIN' "$tmp/server.log" 2>/dev/null && [ $i -lt 150 ]; do sleep 0.1; i=$((i+1)); done
sleep 2
cp "$tmp/dump" "$tmp/2-connected"

t "/menu add \"Test\""
t "/menu -p0 add \"Test/_Hello\" \"echo hello\""
t "/menu -t1 add \"Test/Toggle\" \"echo on\" \"echo off\""
# -rSTATE,GROUP: GROUP is the label of the first item of the group
t "/menu -r1,RadioA add \"Test/RadioA\" \"echo a\""
t "/menu -r0,RadioA add \"Test/RadioB\" \"echo b\""
t "/menu -k4,104 add \"Test/WithKey\" \"echo key\""
t "/menu add \"Test/-\""
t "/menu add \"Test/Gone\" \"echo gone\""
# re-ADDing an existing item with -e0 updates it (fe_menu_update)
t "/menu -e0 -t1 add \"Test/Toggle\" \"echo on\" \"echo off\""
t "/menu del \"Test/Gone\""
sleep 1
cp "$tmp/dump" "$tmp/3-menu-api"

{
	echo "## 1 startup"; cat "$tmp/1-startup"
	echo "## 2 connected"; cat "$tmp/2-connected"
	echo "## 3 /MENU"; cat "$tmp/3-menu-api"
} > "$tmp/actual"

if [ "$update" = "--update" ]; then
	cp "$tmp/actual" "$base"
	echo "menus: baseline written ($(wc -l < "$base" | tr -d ' ') lines)"
	exit 0
fi
if ! diff -u "$base" "$tmp/actual"; then
	echo "menus: FAIL (diff above; tests/ux/menus.sh BIN --update to accept)"
	exit 1
fi
echo "menus: ok"

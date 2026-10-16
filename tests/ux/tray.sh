#!/bin/sh
# The tray icon's menu (built as a right-click on the icon would; Xvfb has
# no panel to click). Checks the items, mnemonics, the Blink-on toggles'
# state, Away/Back sensitivity, and that a toggle's state round-trips into
# the prefs. Baseline: tray.expected.
# usage: tests/ux/tray.sh BIN [--update]   (needs Xvfb + XTest)
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1; update=${2:-}
here=$(cd "$(dirname "$0")" && pwd)
base="$here/tray.expected"
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:75}
tmp=$(mktemp -d)
app=; xv=
cleanup() { kill $app $xv 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT

start_xvfb "$xvfb" "$disp"
mkdir -p "$tmp/cfg"
printf 'gui_slist_skip = 1\nirc_nick1 = shot\ngui_win_left = 0\ngui_win_top = 0\ngui_win_width = 900\ngui_win_height = 600\ninput_tray_priv = 1\ninput_tray_chans = 0\n' > "$tmp/cfg/hexchat.conf"
DISPLAY=$disp NEXCHAT_TRAY_MENU_DUMP="$tmp/dump" "$bin" -a -d "$tmp/cfg" >"$tmp/log" 2>&1 &
app=$!
wait_window "$disp"

x() { DISPLAY=$disp python3 "$here/xkey.py" --delay 0.6 --at 450,580 "$@"; }
x --type "/traymenu-dump" Return
sleep 1
[ -s "$tmp/dump" ] || { echo "tray: FAIL, no menu dump"; cat "$tmp/log" | tail; exit 1; }
cp "$tmp/dump" "$tmp/actual"
# /set flips a Blink-on pref; the menu must show it next time
x --type "/set -quiet input_tray_chans 1" Return
sleep 1
x --type "/traymenu-dump" Return
sleep 1
{ echo "## after input_tray_chans 1"; cat "$tmp/dump"; } >> "$tmp/actual"

if [ "$update" = "--update" ]; then
	cp "$tmp/actual" "$base"
	echo "tray: baseline written ($(wc -l < "$base" | tr -d ' ') lines)"
	exit 0
fi
if ! diff -u "$base" "$tmp/actual"; then
	echo "tray: FAIL (diff above; tests/ux/tray.sh BIN --update to accept)"
	exit 1
fi
echo "tray: ok"

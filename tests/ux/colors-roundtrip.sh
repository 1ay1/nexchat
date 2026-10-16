#!/bin/sh
# colors.conf must come out of a load + Preferences>OK save byte-identical.
# usage: tests/ux/colors-roundtrip.sh path/to/nexchat   (needs Xvfb + XTest)
set -eu
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
xvfb=${XVFB:-/opt/X11/bin/Xvfb}
disp=${SHOT_DISPLAY:-:94}
tmp=$(mktemp -d)
trap 'kill $app $xv 2>/dev/null || true; rm -rf "$tmp"' EXIT

"$xvfb" "$disp" -screen 0 1100x720x24 -nolisten tcp >/dev/null 2>&1 &
xv=$!
sleep 1.5
mkdir -p "$tmp/cfg"
printf 'gui_slist_skip = 1\nirc_nick1 = shot\ngui_win_left = 0\ngui_win_top = 0\n' > "$tmp/cfg/hexchat.conf"

# awkward values on purpose: odd low bytes, 0001, fffe, ffff
python3 - "$tmp/cfg/colors.conf" <<'EOF'
import sys
lines = []
for i in list(range(32)) + list(range(256, 266)):
    r = (i * 2011 + 1) % 65536
    g = (i * 4093) % 65536 or 1
    b = 0xfffe if i % 3 == 0 else (0xffff - i * 977) % 65536
    lines.append("color_%d = %04x %04x %04x\n" % (i, r, g, b))
open(sys.argv[1], "w").write("".join(lines))
EOF
sort "$tmp/cfg/colors.conf" > "$tmp/before"
# a marker line nexchat doesn't write: if it survives, nothing saved the file
echo "# not-from-nexchat" >> "$tmp/cfg/colors.conf"

DISPLAY=$disp "$bin" -a -d "$tmp/cfg" >"$tmp/log" 2>&1 &
app=$!
sleep 5
DISPLAY=$disp python3 "$here/xkey.py" --delay 0.8 --at 400,380 alt+e p
sleep 1
# OK writes colors.conf (palette_save) even if nothing changed
DISPLAY=$disp python3 "$here/xkey.py" --delay 0.8 --at 400,380 alt+o
sleep 2

sort "$tmp/cfg/colors.conf" > "$tmp/after"
if grep -q not-from-nexchat "$tmp/cfg/colors.conf"; then
	echo "FAIL: colors.conf was not rewritten (Preferences didn't save?)"; exit 1
fi
if ! diff -u "$tmp/before" "$tmp/after"; then
	echo "FAIL: colors.conf changed after load+save"
	exit 1
fi
echo "colors-roundtrip: ok ($(wc -l < "$tmp/after" | tr -d ' ') entries)"

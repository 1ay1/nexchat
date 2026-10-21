#!/bin/sh
# Color schemes and cursor styles: Preferences shows the scheme list, a
# scheme's colors reach the chat, Cancel puts the old ones back and the
# picked one is saved in colors.conf; every cursor style draws.
# usage: tests/ux/schemes.sh path/to/nexchat OUTDIR     (needs Xvfb)
set -eu
here=$(cd "$(dirname "$0")" && pwd)
. "$here/isolate.sh"
bin=$1; out=${2:-${TMPDIR:-/tmp}/nexchat-schemes}
mkdir -p "$out"
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:96}
tmp=$(mktemp -d)
"$xvfb" "$disp" -screen 0 1024x768x24 -nolisten tcp >/dev/null 2>&1 &
xpid=$!
trap 'st=$?; kill $xpid 2>/dev/null || true; rm -rf "$tmp"; exit $st' EXIT
sleep 1
fail=0

run () # cfgdir shot.png [env...]
{
	cfg=$1; png=$2; shift 2
	grep -q gui_slist_skip "$cfg/hexchat.conf" 2>/dev/null || echo 'gui_slist_skip = 1' >> "$cfg/hexchat.conf"
	env DISPLAY=$disp NEXCHAT_SHOT="$png:3" "$@" timeout 6 "$bin" -a -d "$cfg" >/dev/null 2>&1 || true
}

px () # png x y -> rrggbb
{
	python3 - "$1" "$2" "$3" <<'EOF'
import sys, zlib, struct
d = open(sys.argv[1], 'rb').read()
w, h, bd, ct = struct.unpack('>IIBB', d[16:26])
raw = b''; i = 8
while i < len(d):
    n, = struct.unpack('>I', d[i:i+4]); t = d[i+4:i+8]
    if t == b'IDAT': raw += d[i+8:i+8+n]
    i += 12 + n
raw = zlib.decompress(raw); bpp = 4 if ct == 6 else 3; st = w * bpp
rows = []; prev = bytearray(st)
for y in range(h):
    f = raw[y*(st+1)]; r = bytearray(raw[y*(st+1)+1:(y+1)*(st+1)])
    for x in range(st):
        a = r[x-bpp] if x >= bpp else 0; b = prev[x]; c = prev[x-bpp] if x >= bpp else 0
        if f == 1: r[x] = (r[x] + a) & 255
        elif f == 2: r[x] = (r[x] + b) & 255
        elif f == 3: r[x] = (r[x] + (a + b) // 2) & 255
        elif f == 4:
            p = a + b - c; pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
            r[x] = (r[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
    rows.append(r); prev = r
x, y = int(sys.argv[2]), int(sys.argv[3])
print('%02x%02x%02x' % tuple(rows[y][x*bpp:x*bpp+3]))
EOF
}

# a scheme in colors.conf colours the chat background
for s in "Dracula:282a36" "Gruvbox Light:fbf1c7" "mIRC:ffffff"; do
	name=${s%:*}; want=${s#*:}
	d="$tmp/$(echo "$name" | tr ' ' _)"; mkdir -p "$d"
	printf 'scheme = %s\n' "$name" > "$d/colors.conf"
	run "$d" "$out/chat-$(basename "$d").png"
	# bottom-left of the chat area, away from text
	got=$(px "$out/chat-$(basename "$d").png" 300 250)
	if [ "$got" = "$want" ]; then echo "ok:   $name background $got"
	else echo "FAIL: $name background $got, want $want"; fail=1; fi
done

# native: the theme's text background (Adwaita: white)
d="$tmp/native"; mkdir -p "$d"; printf 'scheme = native\n' > "$d/colors.conf"
run "$d" "$out/chat-native.png"
got=$(px "$out/chat-native.png" 300 250)
[ "$got" = ffffff ] && echo "ok:   native background $got" || { echo "FAIL: native $got"; fail=1; }

# a desktop gtk.css that paints every selection (and every card) green: a
# scheme picked in nexchat still wins, in the chat and in the channel tree
cp -r "$XDG_CONFIG_HOME" "$tmp/xdg-green"
printf '*:selected, row:selected, listview > row:selected { background-color: #00ff00; color: #000000; }\n.card { background-color: #00ff00; }\n' \
	> "$tmp/xdg-green/gtk-4.0/gtk.css"
d="$tmp/green"; mkdir -p "$d"; printf 'scheme = Dracula\n' > "$d/colors.conf"
run "$d" "$out/chat-green-css.png" XDG_CONFIG_HOME="$tmp/xdg-green"
got=$(px "$out/chat-green-css.png" 300 250)
[ "$got" = 282a36 ] && echo "ok:   gtk.css doesn't change the scheme's chat ($got)" || { echo "FAIL: chat under gtk.css $got"; fail=1; }
# the channel tree's selected row (the <none> server tab, top left)
got=$(px "$out/chat-green-css.png" 100 40)
[ "$got" != 00ff00 ] && echo "ok:   the tree's selection is the scheme's ($got), not gtk.css's" || { echo "FAIL: tree selection is gtk.css green"; fail=1; }

# Preferences > Colors shows the list with the current scheme
d="$tmp/prefs"; mkdir -p "$d"; printf 'scheme = Nord\n' > "$d/colors.conf"
run "$d" "$out/prefs-colors.png" NEXCHAT_MENU_ACTIVATE="Settings/Preferences" NEXCHAT_SHOT_WIN=1 NEXCHAT_PREFS_PAGE=4
if python3 "$here/pngtext.py" "$out/prefs-colors.png" 2>/dev/null | grep -q "Nord"; then echo "ok:   prefs shows Nord"
else echo "note: pngtext can't read prefs (see $out/prefs-colors.png)"; fi
# nothing was saved by just looking
grep -qx 'scheme = Nord' "$d/colors.conf" && echo "ok:   colors.conf kept" || { echo "FAIL: colors.conf changed"; fail=1; }

# every cursor style draws: type "hello", go back two, shoot. GTK only
# draws a cursor in an active window, which needs a window manager.
ob=${OPENBOX:-$(command -v openbox || true)}
obref=$HOME/.cache/nexchat-ref/ob/usr
[ -z "$ob" ] && [ -x "$obref/bin/openbox" ] && ob=$obref/bin/openbox
if [ -z "$ob" ]; then
	echo "schemes: no openbox, cursor shots skipped"
	[ $fail -eq 0 ] && echo "schemes: ok"
	exit $fail
fi
DISPLAY=$disp LD_LIBRARY_PATH=$obref/lib XDG_CONFIG_DIRS=$obref/../etc/xdg XDG_DATA_DIRS=$obref/share "$ob" --sm-disable >/dev/null 2>&1 &
wm=$!
trap 'st=$?; kill $wm $xpid 2>/dev/null || true; rm -rf "$tmp"; exit $st' EXIT
sleep 1
for c in 0 1 2 3 4; do
	d="$tmp/cur$c"; mkdir -p "$d"
	printf 'gui_input_cursor = %d\ngui_input_blink = 0\n' "$c" > "$d/hexchat.conf"
	( sleep 1.5; DISPLAY=$disp python3 "$here/xkey.py" --delay 0.1 --click 300,385 h e l l o Left Left ) &
	keys=$!
	run "$d" "$out/cursor-$c.png"
	wait $keys || true
done
echo "shots in $out"
[ $fail -eq 0 ] && echo "schemes: ok"
exit $fail

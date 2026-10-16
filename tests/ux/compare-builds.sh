#!/bin/sh
# Screenshot the main window and a set of dialogs with two builds side by
# side (same moment, so clocks match) and report the pixels that differ.
# usage: tests/ux/compare-builds.sh OLD_BIN NEW_BIN OUTDIR
set -eu
old=$1; new=$2; out=$3
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out/old" "$out/new"

# name|menu item (Menu/Item, chosen through NEXCHAT_MENU_ACTIVATE) or keys
# (xkey.py; for menus left open)|extra conf
cat > "$out/cases" <<'EOF'
main||
netlist|nexchat/Network List|
prefs|Settings/Preferences|
keys|Settings/Keyboard Shortcuts|
textevents|Settings/Text Events|
autoreplace|Settings/Auto Replace|
dcc|Window/File Transfers|
ignore|Window/Ignore List|
urlgrab|Window/URL Grabber|
plugins|Window/Plugins and Scripts|
rawlog|Window/Raw Log|
chanchart|Window/Character Chart|
friends|Window/Friends List|
banlist|Window/Ban List|
chanlist|Server/Channel List|
about|Help/About|
menu-file|keys:alt+x|
menu-view|keys:alt+v|
menu-window|keys:alt+w|
EOF

n=0
while IFS='|' read -r name what conf; do
	[ -n "$name" ] || continue
	n=$((n + 1))
	keys=; item=
	case $what in keys:*) keys=${what#keys:};; *) item=$what;; esac
	NEXCHAT_MENU_ACTIVATE="$item" KEYS="$keys" SHOT_DISPLAY=":$((60 + 2 * n))" sh "$here/shot-dialog.sh" "$old" "$out/old/$name.png" "" "$conf" >/dev/null 2>&1 &
	NEXCHAT_MENU_ACTIVATE="$item" KEYS="$keys" SHOT_DISPLAY=":$((61 + 2 * n))" sh "$here/shot-dialog.sh" "$new" "$out/new/$name.png" "" "$conf" >/dev/null 2>&1 &
	wait
done < "$out/cases"

python3 - "$out" <<'EOF'
import os, struct, sys, zlib
out = sys.argv[1]
def load(p):
    d = open(p, 'rb').read(); i = 8; idat = b''
    while i < len(d):
        n, = struct.unpack('>I', d[i:i+4]); t = d[i+4:i+8]; c = d[i+8:i+8+n]; i += 12 + n
        if t == b'IHDR': w, h = struct.unpack('>II', c[:8])
        if t == b'IDAT': idat += c
    raw = zlib.decompress(idat); s = w * 3 + 1
    return w, h, [raw[y*s+1:(y+1)*s] for y in range(h)]
bad = 0
for f in sorted(os.listdir(out + '/new')):
    if not os.path.exists(out + '/old/' + f):
        print(f, 'missing in old'); bad += 1; continue
    w, h, A = load(out + '/old/' + f); _, _, B = load(out + '/new/' + f)
    d = [(x, y) for y in range(h) if A[y] != B[y] for x in range(w) if A[y][3*x:3*x+3] != B[y][3*x:3*x+3]]
    if not d:
        print(f, 'identical')
    else:
        xs = [p[0] for p in d]; ys = [p[1] for p in d]
        print(f, len(d), 'px differ, box', min(xs), min(ys), max(xs), max(ys)); bad += 1
sys.exit(1 if bad else 0)
EOF

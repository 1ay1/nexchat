#!/bin/sh
# Every window and dialog, GTK3 and GTK4 side by side, for eyeballing.
# Connected to tests/fake_ircd.py --demo, with notify/ignore entries so
# lists aren't empty. Writes OUT/<name>.png (left GTK3, right GTK4).
# usage: tests/ux/all-windows.sh GTK3_BIN GTK4_BIN OUT
set -eu
g3=$1; g4=$2; out=$3
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out/g3" "$out/g4" "$out/files"
printf 'alice\nbob\n' > "$out/files/notify.conf"
printf 'mask = spam!*@*\ntype = 63\n\nmask = troll!*@*\ntype = 1\n\n' > "$out/files/ignore.conf"

# name|menu path|connect (1/0, nochan: connected but no channel, which
# opens the "connection complete" dialog)
cat > "$out/cases" <<'EOF'
main||1
netlist|nexchat/Network List|0
prefs|Settings/Preferences|0
autoreplace|Settings/Auto Replace|0
ctcp|Settings/CTCP Replies|0
dialogbuttons|Settings/Dialog Buttons|0
keys|Settings/Keyboard Shortcuts|0
textevents|Settings/Text Events|0
urlhandlers|Settings/URL Handlers|0
usercommands|Settings/User Commands|0
ulistbuttons|Settings/User List Buttons|0
ulistpopup|Settings/User List Popup|0
banlist|Window/Ban List|1
chanchart|Window/Character Chart|0
directchat|Window/Direct Chat|0
dcc|Window/File Transfers|0
friends|Window/Friends List|0
ignore|Window/Ignore List|0
plugins|Window/Plugins and Scripts|0
rawlog|Window/Raw Log|1
urlgrab|Window/URL Grabber|1
chanlist|Server/Channel List|1
join|Server/Join a Channel…|1
about|Help/About|0
joindialog||nochan
EOF

n=0
while IFS='|' read -r name item conn; do
	[ -n "$name" ] || continue
	n=$((n + 1))
	c=; [ "$conn" = 1 ] && c=1; [ "$conn" = nochan ] && c=nochan
	SHOT_CONNECT=$c SHOT_WAIT=7 SHOT_FILES="$out/files" NEXCHAT_MENU_ACTIVATE="$item" \
		SHOT_DISPLAY=":$((40 + 2 * n))" sh "$here/shot-dialog.sh" "$g3" "$out/g3/$name.png" "" "" \
		>"$out/g3/$name.log" 2>&1 &
	SHOT_CONNECT=$c SHOT_WAIT=7 SHOT_FILES="$out/files" NEXCHAT_MENU_ACTIVATE="$item" \
		SHOT_DISPLAY=":$((41 + 2 * n))" sh "$here/shot-dialog.sh" "$g4" "$out/g4/$name.png" "" "" \
		>"$out/g4/$name.log" 2>&1 &
	# four at a time
	[ $((n % 2)) -eq 0 ] && wait
done < "$out/cases"
wait

python3 - "$out" <<'PYEOF'
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
def save(p, w, h, rows):
    raw = b''.join(b'\0' + r for r in rows)
    ch = lambda t, c: struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c))
    open(p, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                        + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))
def bbox(w, h, R):
    # the area that isn't the black root window
    xs = [x for x in range(w) if any(R[y][3*x:3*x+3] != b'\0\0\0' for y in range(0, h, 4))]
    ys = [y for y in range(h) if any(R[y][3*x:3*x+3] != b'\0\0\0' for x in range(0, w, 4))]
    return (max(xs) + 1 if xs else w, max(ys) + 1 if ys else h)
for name in sorted(os.listdir(os.path.join(out, 'g3'))):
    if not name.endswith('.png'): continue
    a = os.path.join(out, 'g3', name); b = os.path.join(out, 'g4', name)
    if not os.path.exists(b): continue
    wa, ha, A = load(a); wb, hb, B = load(b)
    ca, cb = bbox(wa, ha, A), bbox(wb, hb, B)
    W = max(ca[0], cb[0]); H = max(ca[1], cb[1])
    pad = lambda R, cw: [R[y][:3*cw] + b'\x40' * 3 * (W - cw) if y < len(R) else b'\x40' * 3 * W for y in range(H)]
    PA = pad(A, ca[0]); PB = pad(B, cb[0])
    save(os.path.join(out, name), 2 * W + 6, H, [pa + b'\xff\x00\xff' * 6 + pb for pa, pb in zip(PA, PB)])
    print(name, 'g3', ca, 'g4', cb)
PYEOF
grep -l CRITICAL "$out"/g3/*.log "$out"/g4/*.log 2>/dev/null | sed 's/^/CRITICAL in /' || true

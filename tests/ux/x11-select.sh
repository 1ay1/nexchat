#!/bin/sh
# The chat box driven by real input events (XTest on a private Xvfb), so
# GTK's own gestures decide what's a click and what's a drag, exactly as
# with a hand: select by dragging, click elsewhere (still, and with a pixel
# or two of jitter) and the selection must be gone, nothing left half drawn,
# and the pointer the I-beam everywhere in the text, the hand on a link.
# Needs a GTK with the X11 backend (osx/build-gtk.sh can't give one on
# macOS: tests/ux/x11-gtk.sh builds it).
# usage: tests/ux/x11-select.sh path/to/nexchat-built-for-x11
set -eu
bin=$1
here=$(cd "$(dirname "$0")/../.." && pwd)
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:77}
tmp=$(mktemp -d)
srv=; app=; xv=
trap 'st=$?; [ -n "$app$srv$xv" ] && kill $app $srv $xv 2>/dev/null; rm -rf "$tmp"; exit $st' EXIT
export GDK_BACKEND=x11 GTK_A11Y=none DISPLAY=$disp

"$xvfb" "$disp" -screen 0 1000x700x24 -nolisten tcp >/dev/null 2>&1 &
xv=$!
sleep 1.5

python3 - "$tmp/script" <<'EOF'
import sys
lines = [":p%d!p@h PRIVMSG #x :line %d with words to select across a row or two" % (i, i) for i in range(30)]
lines.append(":d!d@h PRIVMSG #x :a link https://example.org/path here")
open(sys.argv[1], "w").write("\n".join(lines) + "\n")
EOF
python3 "$here/tests/fake_ircd.py" --timeout 90 --log "$tmp/s.log" --portfile "$tmp/port" --script "$tmp/script" >/dev/null 2>&1 &
srv=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
mkdir "$tmp/cfg"
printf 'gui_slist_skip = 1\nirc_nick1 = x\ngui_win_left = 0\ngui_win_top = 0\ngui_win_width = 900\ngui_win_height = 600\ngui_win_save = 0\ngui_tab_layout = 0\n' > "$tmp/cfg/hexchat.conf"
NEXCHAT_XCHECK="$tmp/check" "$bin" -n -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/#x" >"$tmp/log" 2>&1 &
app=$!
i=0; while ! grep -q '^xtext-at:' "$tmp/log" && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
sleep 2
set -- $(sed -n 's/^xtext-at: //p' "$tmp/log" | head -1)
X=${1:-0}; Y=${2:-0}; W=${3:-600}; H=${4:-400}
echo "text box at $X,$Y ${W}x$H"

fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }
xk() { python3 "$here/tests/ux/xkey.py" "$@" >/dev/null 2>&1; }
check() # -> "diff stray? sel"
{
	touch "$tmp/check"
	i=0; while [ -f "$tmp/check" ] && [ $i -lt 30 ]; do sleep 0.1; i=$((i+1)); done
	grep '^xcheck:' "$tmp/log" | tail -1
}
cursor() # what X shows over the text box now
{
	python3 - <<'EOF'
import ctypes
x = ctypes.CDLL("/opt/X11/lib/libX11.6.dylib")
xf = ctypes.CDLL("/opt/X11/lib/libXfixes.3.dylib")
x.XOpenDisplay.restype = ctypes.c_void_p
class Img(ctypes.Structure):
    _fields_ = [("x", ctypes.c_short), ("y", ctypes.c_short), ("width", ctypes.c_ushort),
                ("height", ctypes.c_ushort), ("xhot", ctypes.c_ushort), ("yhot", ctypes.c_ushort),
                ("serial", ctypes.c_ulong), ("pixels", ctypes.c_void_p), ("atom", ctypes.c_ulong),
                ("name", ctypes.c_char_p)]
xf.XFixesGetCursorImage.restype = ctypes.POINTER(Img)
d = x.XOpenDisplay(None)
img = xf.XFixesGetCursorImage(ctypes.c_void_p(d))
print((img.contents.name or b"?").decode(), img.contents.width, img.contents.height)
EOF
}

row() { echo $((Y + 10 + $1 * 17)); }

# 1. drag across rows
xk --drag $((X + 40)),$(row 2):$((X + 420)),$(row 6)
sleep 0.5
r=$(check); echo "  after drag: $r"
case "$r" in "xcheck: 0 "?*) ok "drag selects, nothing half drawn";; *) bad "drag: $r";; esac

# 2. click elsewhere, still
xk --click $((X + 300)),$(row 10)
sleep 0.5
r=$(check); echo "  after click: $r"
[ "$r" = "xcheck: 0 " ] && ok "a click elsewhere deselects" || bad "click didn't deselect: $r"

# 3. select again, then a click with a 2px jitter (press, tiny move, release)
xk --drag $((X + 40)),$(row 2):$((X + 420)),$(row 6)
sleep 0.4
xk --drag $((X + 300)),$(row 10):$((X + 302)),$(row 10)
sleep 0.5
r=$(check); echo "  after jittery click: $r"
[ "$r" = "xcheck: 0 " ] && ok "a click with a little hand jitter deselects" || bad "jittery click: $r"

# 4. the pointer: I-beam on text, past a line's end, below the last line
for p in "$((X + 60)) $(row 3)" "$((X + W - 20)) $(row 3)" "$((X + 60)) $((Y + H - 10))"; do
	set -- $p
	xk --move $1,$2
	sleep 0.4
	c=$(cursor)
	case "$c" in xterm*|text*) ok "I-beam at $1,$2 ($c)";; *) bad "pointer at $1,$2 is '$c', want the I-beam";; esac
done

n=$(grep -c CRITICAL "$tmp/log" || true)
[ "$n" -eq 0 ] && ok "no criticals" || bad "$n criticals"
[ $fail -eq 0 ] && echo "x11-select: ok"
exit $fail

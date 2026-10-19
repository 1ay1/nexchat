#!/bin/sh
# Scrolling the chat box: a trackpad moves the text pixel by pixel (the
# scroll position goes fractional, rows drawn at the in-between offset
# match a full redraw), a wheel steps whole lines, a flick glides on and
# stops, and nothing is ever left half drawn.
# usage: tests/ux/scroll.sh path/to/nexchat
set -eu
bin=$1
here=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
srv=
trap 'st=$?; [ -n "$srv" ] && { kill $srv 2>/dev/null || true; }; rm -rf "$tmp"; exit $st' EXIT
export GTK_A11Y=none G_DEBUG=fatal-criticals

python3 - "$tmp/script" <<'EOF'
import sys
lines = [":u%d!u@h PRIVMSG #s :line %d \U0001F600 with some text and \x02bold\x02 and \x0304red\x03" % (i, i) for i in range(200)]
open(sys.argv[1], "w", encoding="utf-8").write("\n".join(lines) + "\n")
EOF
python3 "$here/tests/fake_ircd.py" --timeout 60 --log "$tmp/s.log" --portfile "$tmp/port" --script "$tmp/script" >/dev/null 2>&1 &
srv=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
mkdir "$tmp/cfg"
printf 'gui_slist_skip = 1\nirc_nick1 = scr\ngui_win_width = 760\ngui_win_height = 420\n' > "$tmp/cfg/hexchat.conf"
NEXCHAT_XDRAG="wait 90;pos 0 0;pixscroll 0 -7;pos 1 0;pixscroll 0 -7;pixscroll 0 -7;pos 2 0;wheel 0 -1;pos 3 0;wheel 0 1;pos 4 0;flick 0 -2500;wait 40;pos 5 0;pixscroll 0 9999;pos 6 0" \
	NEXCHAT_XDRAG_QUIT=1 timeout 40 "$bin" -n -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/#s" >"$tmp/log" 2>&1 || true

fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }
pos() { sed -n "s/^xdrag-pos: $1 \([^ ]*\) .*/\1/p" "$tmp/log"; }
dif() { sed -n "s/^xdrag-pos: $1 [^ ]* //p" "$tmp/log"; }
p0=$(pos 0); p1=$(pos 1); p2=$(pos 2); p3=$(pos 3); p4=$(pos 4); p5=$(pos 5); p6=$(pos 6)
echo "  positions: $p0 $p1 $p2 $p3 $p4 $p5 $p6"
[ -n "$p0" ] || { bad "no result"; grep -v "^$" "$tmp/log" | tail -5; exit 1; }
python3 - "$p0" "$p1" "$p2" "$p3" "$p4" "$p5" "$p6" <<'EOF' || fail=1
import sys
p = [float(v) for v in sys.argv[1:]]
bad = 0
def chk(c, m):
    global bad
    print(("ok:   " if c else "FAIL: ") + m)
    bad |= not c
chk(0 < p[0] - p[1] < 1, "7 px of trackpad moves less than a line (%.3f lines)" % (p[0] - p[1]))
chk(abs((p[0] - p[2]) - 3 * (p[0] - p[1])) < 0.01, "pixel moves add up exactly")
chk(p[2] - p[3] > 1, "a wheel step moves a few lines (%.2f)" % (p[2] - p[3]))
chk(abs(p[4] - p[2]) < 0.01, "and back the same")
chk(p[5] < p[4] - 3, "a flick glides on (%.1f lines)" % (p[4] - p[5]))
chk(p[6] >= p[0] - 0.01, "scrolling far down stops at the bottom")
sys.exit(bad)
EOF
for k in 0 1 2 3 4 5 6; do
	d=$(dif $k)
	[ "$d" = 0 ] || bad "step $k: $d pixels differ from a full redraw"
done
[ $fail -eq 0 ] && ok "every step drawn whole" 
n=$(grep -c CRITICAL "$tmp/log" || true)
[ "$n" -eq 0 ] && ok "no criticals" || bad "$n criticals"
[ $fail -eq 0 ] && echo "scroll: ok"
exit $fail

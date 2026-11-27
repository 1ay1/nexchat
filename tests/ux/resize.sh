#!/bin/sh
# Resizing the main window must never leave the chat box wrong: after every
# size (narrower, wider, tiny, huge, height only, back again) the text is
# wrapped for the width it has now, the pixels match a full redraw, a view
# at the bottom stays at the bottom and a view scrolled up keeps the same
# message at the top. The app resizes itself through GTK (NEXCHAT_XDRAG
# "size W H"), so this runs the same on X11 (a private Xvfb) and Wayland
# (under tests/ux/with-weston.sh).
# usage: tests/ux/resize.sh path/to/nexchat
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1
here=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
srv=; xv=
trap 'st=$?; kill $srv $xv 2>/dev/null || true; rm -rf "$tmp"; exit $st' EXIT
if [ -z "${NX_WAYLAND:-}" ]; then
	disp=${DISP:-:67}
	start_xvfb "${XVFB:-$(command -v Xvfb)}" "$disp" 1600 1000
	export DISPLAY=$disp
fi
export G_DEBUG=fatal-criticals

python3 - "$tmp/script" <<'EOF'
import sys
w = "lorem ipsum dolor sit amet consectetur adipiscing elit sed do eiusmod"
lines = []
for i in range(120):
    n = 1 + i % 5
    lines.append(":n%d!u@h PRIVMSG #r :%d %s \U0001F600 \x02bold\x02 \x0304red\x03 %s" % (i % 7, i, " ".join([w] * n), "x" * (i % 30)))
lines.append(":z!u@h PRIVMSG #r :" + "unbroken" * 40)
open(sys.argv[1], "w", encoding="utf-8").write("\n".join(lines) + "\n")
EOF
python3 "$here/tests/fake_ircd.py" --timeout 120 --log "$tmp/s.log" --portfile "$tmp/port" --script "$tmp/script" >/dev/null 2>&1 &
srv=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
mkdir "$tmp/cfg"
printf 'gui_slist_skip = 1\nirc_nick1 = rz\ngui_win_width = 900\ngui_win_height = 600\ngui_win_save = 0\ngui_win_state = 0\ntext_indent = 1\nstamp_text = 1\n' > "$tmp/cfg/hexchat.conf"

# B: at the bottom; K: keeps the message that was at the top
steps="wait 100"
n=0
at() { n=$((n+1)); steps="$steps;size $1 $2;wait 3;lay $n"; echo "$n $1 $2 $3" >> "$tmp/plan"; }
sweep() { for w in "$@"; do steps="$steps;size $w $h"; done; }
# (sizes fit a 1280x800 output: the headless compositors' default)
at 900 600 B; at 500 600 B; at 1250 600 B; at 330 600 B; at 1250 780 B
at 1250 400 B; at 260 220 B; at 900 600 B
h=600; sweep 880 840 790 720 650 600 560 610 700 820 950 1100; at 1200 600 B
h=533; sweep 1200 1000 800 600 400 300 400 600; at 800 533 B
# scroll well up, then resize: the same message stays at the top
steps="$steps;wheel 0 -40;wait 10"
at 800 533 R; at 600 533 K; at 1100 700 K; at 420 700 K
h=700; sweep 500 600 700 800 900; at 1000 700 K; at 800 533 K
# a height-only change keeps it too
at 800 300 K; at 800 533 K

NEXCHAT_XDRAG="$steps" NEXCHAT_XDRAG_QUIT=1 timeout 90 "$bin" -n -d "$tmp/cfg" \
	"irc://127.0.0.1:-$(cat "$tmp/port")/#r" >"$tmp/log" 2>&1 || true

fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }
keep=
while read -r k w h what; do
	r=$(grep "^xlay: $k " "$tmp/log" || true)
	[ -n "$r" ] || { bad "step $k (${w}x$h): no result"; continue; }
	set -- $r   # xlay: N W H BAD VALUE BOTTOM TOP DIFF
	# the window has a minimum size: narrower asks stop there (still checked)
	if [ "$3" -lt "$w" ] || [ "$4" -lt "$h" ] || { [ "$3" != "$w" ] && [ "$4" != "$h" ]; }; then
		bad "${w}x$h: the window became ${3}x$4"; continue
	fi
	w=$3; h=$4
	if [ "$5" != 0 ] || [ "$9" != 0 ]; then
		bad "${w}x$h: $5 entries wrapped for another width, $9 pixels unlike a full redraw"; continue
	fi
	case $what in
	B) if awk "BEGIN { exit !($6 < $7 - 0.01) }"; then bad "${w}x$h: scrolled off the bottom ($6 of $7)"; continue; fi ;;
	R) keep=$8 ;;
	K) if [ "$8" != "$keep" ]; then bad "${w}x$h: the view jumped (top was $keep, now $8)"; continue; fi ;;
	esac
	ok "${w}x$h: wrapped right, drawn whole$([ "$what" = K ] && echo ", same text at the top")"
done < "$tmp/plan"
[ -n "$keep" ] && [ "$keep" != "-" ] || bad "no message at the top to keep"
c=$(grep -c CRITICAL "$tmp/log" || true)
[ "$c" -eq 0 ] && ok "no criticals" || bad "$c criticals"
[ $fail -eq 0 ] && echo "resize: ok ($([ -n "${NX_WAYLAND:-}" ] && echo wayland || echo x11))"
exit $fail

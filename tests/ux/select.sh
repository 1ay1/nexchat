#!/bin/sh
# Selecting text in the chat box leaves no row half-drawn: drags up and
# down over lines with colours, bold, emoji and long wrapped lines, a
# double-click, links hovered on and off, then the box as drawn bit by bit
# must match a full redraw pixel for pixel.
# usage: tests/ux/select.sh path/to/nexchat
set -eu
bin=$1
here=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
srv=; app=
trap 'st=$?; [ -n "$app$srv" ] && kill $app $srv 2>/dev/null; rm -rf "$tmp"; exit $st' EXIT
export GTK_A11Y=none G_DEBUG=fatal-criticals

# a channel with lines of every kind (real mIRC control bytes)
python3 - "$tmp/script" <<'EOF'
import sys
B, C, I, U, O = "\x02", "\x03", "\x1d", "\x1f", "\x0f"
lines = [
 ":alice!a@h PRIVMSG #sel :plain words on a line of their own",
 f":bob!b@h PRIVMSG #sel :{C}04red{C} {C}03green{C} {B}bold{B} {I}italic{I} {U}under{U} done",
 ":carol!c@h PRIVMSG #sel :emoji \U0001F600\U0001F389\U0001F44D and CJK \u6f22\u5b57\u304b\u306a \ud55c\uad6d\uc5b4 mixed in",
 ":dave!d@h PRIVMSG #sel :a link https://example.org/some/long/path?q=1 in the middle of text",
 ":erin!e@h PRIVMSG #sel :a much longer line that goes on and on so it has to wrap onto more than one row in the text box, wrapping again and again, with more words after that to be sure it wraps",
 f":frank!f@h PRIVMSG #sel :{C}08,02yellow on blue{C} then plain again \u00c5\u00c4\u00d6 \u00e0\u00e9\u00ee\u00f5\u00fc \u00f1",
 ":grace!g@h PRIVMSG #sel :tabs\tand\tspaces    lined    up",
 ":heidi!h@h PRIVMSG #sel :short",
 ":ivan!i@h PRIVMSG #sel :one more line \U0001F600 with emoji at the end \U0001F389",
]
open(sys.argv[1], "w", encoding="utf-8").write("\n".join(lines) + "\n")
EOF

fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }

run () # name "steps"
{
	rm -f "$tmp/port"
	python3 "$here/tests/fake_ircd.py" --timeout 40 --log "$tmp/server.log" --portfile "$tmp/port" \
		--script "$tmp/script" >/dev/null 2>&1 &
	srv=$!
	i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
	rm -rf "$tmp/cfg"; mkdir "$tmp/cfg"
	printf 'gui_slist_skip = 1\nirc_nick1 = sel\ngui_win_width = 760\ngui_win_height = 420\nstamp_text = 1\n' > "$tmp/cfg/hexchat.conf"
	NEXCHAT_XDRAG="wait 90;$2" NEXCHAT_XDRAG_PNG="$tmp/$1" NEXCHAT_XDRAG_QUIT=1 \
		timeout 30 "$bin" -n -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/#sel" >"$tmp/$1.log" 2>&1 || true
	kill $srv 2>/dev/null || true; wait $srv 2>/dev/null || true; srv=
	d=$(sed -n 's/^xdrag-diff: //p' "$tmp/$1.log" | tail -1)
	c=$(grep -c CRITICAL "$tmp/$1.log" || true)
	if [ "$d" = 0 ] && [ "$c" -eq 0 ]; then ok "$1: no leftovers"
	else bad "$1: ${d:-no result} pixels differ, $c criticals"
		[ -n "${KEEP:-}" ] && cp "$tmp/$1-now.png" "$KEEP/$1-now.png" 2>/dev/null && cp "$tmp/$1-full.png" "$KEEP/$1-full.png" 2>/dev/null
	fi
}

# steps between: one motion per frame, a hand moving
down=""; for y in 20 35 50 65 80 95 110 125 140 155 170 185 200 215 230; do down="$down;move $((150 + y)) $y"; done
up=""; for y in 215 200 185 170 155 140 125 110 95 80 65 50 35 20 10; do up="$up;move $((400 - y)) $y"; done
back=""; for y in 200 150 100 60 30 60 100 150; do back="$back;move 300 $y"; done
run drag-down "press 120 15$down;release 360 230"
run drag-up "press 400 230$up;release 120 10"
run drag-back "press 200 15$back;move 520 140;release 520 140"
run word "dpress 220 50;release 220 50"
run reselect "press 120 15;move 300 120;release 300 120;press 160 40;move 500 200;release 500 200;press 300 300;release 300 300"
run hover "hover 260 70;hover 300 70;hover 360 70;hover 600 70;hover 260 70;hover 50 300"

[ $fail -eq 0 ] && echo "select: ok"
exit $fail

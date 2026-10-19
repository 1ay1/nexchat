#!/bin/sh
# The real macOS mouse (CoreGraphics events, as a hand would) drags across
# the chat box over lines with emoji, accents and colours; after each drag
# nothing may be left half drawn and the copy must be valid UTF-8.
# Moves the pointer: run it when you're not using the machine. Needs the
# terminal to have Accessibility (to post mouse events) in System Settings >
# Privacy & Security; without it the events are dropped and nothing is
# selected (tests/ux/select.sh and utf8.py check the same through GTK's
# event handlers and need no permission).
# usage: tests/ux/mac-select.sh path/to/nexchat
set -eu
bin=$1
here=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
srv=; app=
trap 'st=$?; [ -n "$app$srv" ] && kill $app $srv 2>/dev/null; rm -rf "$tmp"; exit $st' EXIT
export GTK_A11Y=none
cc "$here/tests/ux/macmouse.c" -o "$tmp/macmouse" -framework ApplicationServices

python3 - "$tmp/script" <<'EOF'
import sys
C, B = "\x03", "\x02"
lines = [
 ":a!a@h PRIVMSG #m :thumbs \U0001F44D\U0001F3FD family \U0001F468\u200d\U0001F469\u200d\U0001F467 flag \U0001F1E9\U0001F1EA",
 f":b!b@h PRIVMSG #m :{C}04red{C} {C}03green{C} {B}bold{B} e\u0301 a\u0300 o\u0308 \u6f22\u5b57\u304b\u306a",
 ":c!c@h PRIVMSG #m :keycap 1\ufe0f\u20e3 heart \u2764\ufe0f and \U0001F600text\U0001F389more\U0001F44Dend",
 ":d!d@h PRIVMSG #m :a long line that wraps over more than one row of the text box so a drag crosses a wrap with emoji \U0001F600 in it \U0001F389 and keeps going",
 ":e!e@h PRIVMSG #m :RTL \u05e9\u05dc\u05d5\u05dd \u0645\u0631\u062d\u0628\u0627 mixed and plain",
]
open(sys.argv[1], "w", encoding="utf-8").write("\n".join(lines) + "\n")
EOF
python3 "$here/tests/fake_ircd.py" --timeout 90 --log "$tmp/s.log" --portfile "$tmp/port" --script "$tmp/script" >/dev/null 2>&1 &
srv=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
mkdir "$tmp/cfg"
# the window at a known place on screen
printf 'gui_slist_skip = 1\nirc_nick1 = msel\ngui_win_left = 200\ngui_win_top = 150\ngui_win_width = 800\ngui_win_height = 460\ngui_win_save = 0\ngui_tab_layout = 0\n' > "$tmp/cfg/hexchat.conf"
NEXCHAT_XCHECK="$tmp/check" NEXCHAT_WINDOWS_DUMP=1 "$bin" -n -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/#m" >"$tmp/log" 2>&1 &
app=$!
i=0; while ! grep -q '^xtext-at:' "$tmp/log" && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
sleep 2
osascript -e 'tell application "System Events" to set frontmost of (first process whose unix id is '"$app"') to true' 2>/dev/null || true
sleep 1
set -- $(sed -n 's/^xtext-at: //p' "$tmp/log" | head -1)
X=${1:-0}; Y=${2:-0}; W=${3:-600}
echo "text box at $X,$Y (${W} wide)"

fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }
n=0
# drags inside the text box, row by row (rows ~16px), across emoji lines,
# up, down, back, across the wrapped line
row() { echo $((Y + 8 + $1 * 16)); }
for d in "40 2 300 2" "40 3 500 4" "500 5 40 3" "40 4 200 4" "60 6 420 6" "40 2 520 7" "520 7 40 2" "100 5 100 2"; do
	set -- $d
	"$tmp/macmouse" drag $((X + $1)) $(row $2) $((X + $3)) $(row $4)
	sleep 0.4
	touch "$tmp/check"
	i=0; while [ -f "$tmp/check" ] && [ $i -lt 30 ]; do sleep 0.1; i=$((i+1)); done
	n=$((n + 1))
done
[ -n "${KEEP:-}" ] && screencapture -x -R$((X - 20)),$((Y - 40)),$((W + 60)),300 "$KEEP/mac-select.png" 2>/dev/null
sleep 0.5
kill $app 2>/dev/null || true; wait $app 2>/dev/null || true; app=

c=$(grep -c '^xcheck:' "$tmp/log" || true)
[ "$c" -eq "$n" ] && ok "$n real drags checked" || bad "only $c of $n drags reported"
python3 - "$tmp/log" <<'EOF' || fail=1
import re, sys
data = open(sys.argv[1], "rb").read()
bad = 0
sels = 0
for diff, sel in re.findall(rb"^xcheck: (-?\d+) (.*)$", data, re.M):
    if diff != b"0":
        print("FAIL: %s pixels left half drawn after a drag" % diff.decode()); bad = 1
    try:
        t = sel.decode("utf-8")
    except UnicodeDecodeError:
        print("FAIL: copied text not valid UTF-8: %r" % sel); bad = 1; continue
    if t:
        sels += 1
    if t.endswith("\u200d"):
        print("FAIL: copy ends inside an emoji: %r" % t[-6:]); bad = 1
if not sels:
    print("FAIL: no drag selected anything (window not where expected?)"); bad = 1
else:
    print("ok:   %d drags selected text; no leftovers, valid UTF-8" % sels)
sys.exit(bad)
EOF
grep -q CRITICAL "$tmp/log" && bad "GTK criticals: $(grep -c CRITICAL "$tmp/log")" || ok "no criticals"
[ $fail -eq 0 ] && echo "mac-select: ok"
exit $fail

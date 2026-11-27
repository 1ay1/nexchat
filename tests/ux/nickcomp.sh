#!/bin/sh
# Nick completion in the input box, the real app: "@" lists the people,
# narrowed as you type, best first; the one who just said your name
# comes before the one who merely talked; a word from inside a nick works
# ("cat" -> CrazyCat); Tab fills in "nick: " at the start of a line and
# lists the people when several fit; Escape closes it.
# usage: tests/ux/nickcomp.sh path/to/nexchat     (needs Xvfb + XTest)
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:68}
tmp=$(mktemp -d)
app=; ircd=; xv=
trap 'kill $app $ircd $xv 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"' EXIT

start_xvfb "$xvfb" "$disp"
mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = yo\ngui_slist_skip = 1\ngui_win_left = 0\ngui_win_top = 0\ngui_win_width = 900\ngui_win_height = 600\ncompletion_suffix = :\ncompletion_auto = 1\n' > "$tmp/cfg/hexchat.conf"
# people join; Nemu64 talks, then CrazyCat says "yo" (your name), then
# Nemu64 talks again: Nemu64 is the latest to talk, CrazyCat mentioned you
cat > "$tmp/script" <<'EOF'
:CrazyCat!u@h JOIN #t
:CrazyCat`!u@h JOIN #t
:Nemu64!u@h JOIN #t
:aerth!u@h JOIN #t
:LoGiQue!u@h JOIN #t
:cbob!u@h JOIN #t
:Nemu64!u@h PRIVMSG #t :hello all
:CrazyCat!u@h PRIVMSG #t :yo, you around?
:cbob!u@h PRIVMSG #t :just talking
EOF
python3 "$here/../fake_ircd.py" --timeout 90 --log "$tmp/server.log" --portfile "$tmp/port" --script "$tmp/script" 2>/dev/null &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
DISPLAY=$disp NEXCHAT_TABS_DUMP=1 \
	"$bin" -a -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/#t" >"$tmp/log" 2>&1 &
app=$!
wait_window "$disp"
wait_for grep -q '^tab: #t' "$tmp/log"
# the script is sent after the JOIN: give it a moment to arrive
sleep 2

k() { DISPLAY=$disp python3 "$here/xkey.py" --delay 0.25 --at 450,580 "$@"; }
t() { DISPLAY=$disp python3 "$here/xkey.py" --delay 0.25 --at 450,580 --type "$1"; }
state() {
	n0=$(grep -c '^tabs:$' "$tmp/log")
	_i=0
	while [ "$(grep -c '^tabs:$' "$tmp/log")" -lt $((n0 + 2)) ]; do
		_i=$((_i+1)); [ $_i -gt 100 ] && { echo "(no dump: app gone?)"; return; }
		sleep 0.1
	done
	awk '/^tabs:$/{n++} {r[n]=r[n] $0 "\n"} END{printf "%s", r[n-1]}' "$tmp/log" | grep -E "^($1):" | head -1
}
fail=0
check() {
	got=$(state "$1")
	case "$got" in
		$2) echo "ok:   $3";;
		*) echo "FAIL: $3 (got: $got)"; fail=1;;
	esac
}
clear_input() { k ctrl+a BackSpace; }

# "@": everyone, the one who said your name first, then who talked last
t "@"
check cmdpopup 'cmdpopup: list: \[CrazyCat\]*cbob*Nemu64*' '"@" lists people, who mentioned you first'
# narrowed as typed; a word inside a nick counts
t "cat"
check cmdpopup 'cmdpopup: list: \[CrazyCat\]*CrazyCat`*' '"@cat" finds CrazyCat by a word inside'
k Return
check input 'input: @CrazyCat ' 'Enter fills in "@CrazyCat "'
clear_input

# a mention in the middle of a line, by digits
t "thanks @64"
check cmdpopup 'cmdpopup: list: \[Nemu64\]*' '"@64" mid-line finds Nemu64'
k Escape
check cmdpopup 'cmdpopup: *' 'Escape closes it'
t "x"
check cmdpopup 'cmdpopup: *' 'and it stays closed for that word'
clear_input

# Tab at the start of a line: one fits -> "nick: "
t "aer"
k Tab
check input 'input: aerth: ' 'Tab on "aer" fills in "aerth: "'
clear_input
# several fit -> the list, best first; Tab takes the pick
t "c"
k Tab
check cmdpopup 'cmdpopup: list: \[CrazyCat\]*cbob*' 'Tab on "c" lists them, who mentioned you first'
k Down Tab
check input 'input: cbob: ' 'Down, Tab takes the second: "cbob: "'
clear_input
# initials
t "lgq"
k Tab
check input 'input: LoGiQue: ' 'Tab on initials "lgq" gives LoGiQue'
clear_input

# you said someone's name: they come first for "c" now
t "cbob: hey"
k Return
wait_for grep -q 'PRIVMSG #t :cbob: hey' "$tmp/server.log"
t "c"
k Tab
check cmdpopup 'cmdpopup: list: \[cbob\]*' 'after you answered cbob, cbob first'
k Escape
clear_input

# completion on send: "lo: hi" goes out as "LoGiQue: hi"
t "lo: hi there"
k Return
wait_for grep -q 'PRIVMSG #t :LoGiQue: hi there' "$tmp/server.log" && echo "ok:   \"lo: hi\" is sent to LoGiQue" \
	|| { echo "FAIL: send-time completion ($(grep 'PRIVMSG #t :' "$tmp/server.log" | tail -1))"; fail=1; }

# not an email address
t "mail me@ex"
check cmdpopup 'cmdpopup: *' '"me@ex" is not a mention'
clear_input

if grep -qiE 'gtk-critical|criticals' "$tmp/log"; then grep -iE 'critical' "$tmp/log" | sort | uniq -c | head; fail=1; fi
[ $fail -eq 0 ] && echo "nickcomp: ok"
exit $fail

#!/bin/sh
# The "/" command list over the input box: typing narrows it, Down moves,
# Tab fills in, then a usage hint while typing the arguments; Escape
# closes it; Enter on a whole command sends it; "//" isn't a command.
# usage: tests/ux/cmdpopup.sh path/to/nexchat     (needs Xvfb + XTest)
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:89}
tmp=$(mktemp -d)
app=; ircd=; xv=
trap 'kill $app $ircd $xv 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"' EXIT

start_xvfb "$xvfb" "$disp"
mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = smoke\ngui_slist_skip = 1\ngui_win_left = 0\ngui_win_top = 0\ngui_win_width = 900\ngui_win_height = 600\n' > "$tmp/cfg/hexchat.conf"
python3 "$here/../fake_ircd.py" --timeout 90 --log "$tmp/server.log" --portfile "$tmp/port" 2>/dev/null &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
DISPLAY=$disp NEXCHAT_TABS_DUMP=1 \
	"$bin" -a -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/#t" >"$tmp/log" 2>&1 &
app=$!
wait_window "$disp"
wait_for grep -q '^tab: #t' "$tmp/log"
sleep 0.5

k() { DISPLAY=$disp python3 "$here/xkey.py" --delay 0.25 --at 450,580 "$@"; }
t() { DISPLAY=$disp python3 "$here/xkey.py" --delay 0.25 --at 450,580 --type "$1"; }
# the popup / input as of a full dump taken after the last keys
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

# just "/": everything, the everyday commands first under a heading, with
# the first one explained in plain words and an example
t "/"
check cmdpopup 'cmdpopup: list: {Most used} \[*' '"/" alone lists commands, everyday ones first'
check cmdpopup '*+[0-9]* | details: */* Example:*' 'the selected one is explained, with an example'
k Down
check cmdpopup 'cmdpopup: list: {Most used} * \[*' 'Down moves, and the explanation follows'
k BackSpace

# options and choices come up for the argument that takes them
t "/lastlog "
check cmdpopup 'cmdpopup: list: \[-a\] -m -r -h*' '/lastlog offers its options'
k ctrl+a BackSpace
t "/ignore spammer "
check cmdpopup 'cmdpopup: list: \[ALL\] PRIV CHAN*' '/ignore nick offers what to ignore'
k ctrl+a BackSpace
t "/dcc "
check cmdpopup 'cmdpopup: list: \[SEND\] GET CHAT LIST CLOSE*' '/dcc offers its subcommands'
k ctrl+a BackSpace

t "/jo"
check cmdpopup 'cmdpopup: list: \[JOIN\]*' '/jo lists JOIN first, selected'
# exactly as wide as the box holding the composer, right on top of it
# (cmdgeom: pop x w bottom | row x w top)
geom=$(state cmdgeom)
set -- $(echo "$geom" | sed 's/[^0-9 ]//g')
if [ "$#" -ge 6 ] && [ "$1" = "$4" ] && [ "$2" = "$5" ] && [ $(( $6 - $3 )) -ge 0 ] && [ $(( $6 - $3 )) -le 2 ]; then
	echo "ok:   on top of the composer's row, same width ($geom)"
else
	echo "FAIL: placement ($geom)"; fail=1
fi
# hide the user list (Ctrl+F7) with the list open: the row gets wider
# and the list follows
k ctrl+F7
geom2=$(state cmdgeom)
set -- $(echo "$geom2" | sed 's/[^0-9 ]//g')
if [ "$#" -ge 6 ] && [ "$1" = "$4" ] && [ "$2" = "$5" ] && [ "$geom2" != "$geom" ]; then
	echo "ok:   follows the row when the user list hides ($geom2)"
else
	echo "FAIL: after hiding the user list ($geom2)"; fail=1
fi
k ctrl+F7
k BackSpace
check cmdpopup 'cmdpopup: list: \[J*' '/j still lists, J commands first'
k Down
check cmdpopup 'cmdpopup: list: J* \[*' 'Down moves to the next one'
k Up
t "o"
k Tab
check input 'input: /join ' 'Tab fills in "/join " (lower case, as typed)'
check cmdpopup 'cmdpopup: list: \[#t\] | hint: /JOIN <#channel>*' 'after /join: your channels, under the usage line'
k Escape
check cmdpopup 'cmdpopup: ' 'Escape closes it'
t "#x"
check cmdpopup 'cmdpopup: ' 'stays closed while typing on'
k Return
sleep 1
grep -q '^JOIN #x' "$tmp/server.log" && echo "ok:   Enter sends /join #x" || { echo "FAIL: /join #x not sent"; fail=1; }

t "/away"
check cmdpopup 'cmdpopup: list: \[AWAY\]*' '/away lists AWAY'
k Return
sleep 1
grep -q '^AWAY' "$tmp/server.log" && echo "ok:   Enter on a whole name sends it" || { echo "FAIL: /away not sent"; fail=1; }
check input 'input: ' 'box empty after sending'

t "//"
check cmdpopup 'cmdpopup: ' '"//" (a plain slash) shows nothing'
k ctrl+a BackSpace

# a nick argument: the people in the channel (the fake server has bob)
t "/kick b"
check cmdpopup 'cmdpopup: list: \[bob\] | hint: /KICK <nick>*' '/kick b offers bob'
k Tab
check input 'input: /kick bob ' 'Tab puts the nick in, then a space'
check cmdpopup 'cmdpopup: hint: /KICK <nick> \[reason\]*' 'on to the reason: just the usage'
k ctrl+a BackSpace

# /set: the setting names
t "/set gui_tab_u"
check cmdpopup 'cmdpopup: list: \[gui_tab_unread\]*' '/set gui_tab_u offers gui_tab_unread'
k ctrl+a BackSpace

# /help: the commands
t "/help joi"
check cmdpopup 'cmdpopup: list: \[JOIN\]*' '/help joi offers JOIN'
k ctrl+a BackSpace
t "hello"
check cmdpopup 'cmdpopup: ' 'plain text shows nothing'

if [ $fail -ne 0 ]; then
	echo "--- last dump"; awk '/^tabs:$/{n++} {r[n]=r[n] $0 "\n"} END{printf "%s", r[n-1]}' "$tmp/log"
	grep -iE 'critical|warning|segfault|abort' "$tmp/log" | sort | uniq -c | head
	tail -5 "$tmp/log"
	exit 1
fi
if grep -qiE 'gtk-critical|criticals' "$tmp/log"; then grep -iE 'critical' "$tmp/log" | sort | uniq -c | head; exit 1; fi
echo "cmdpopup: ok"

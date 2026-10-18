#!/bin/sh
# draft/multiline sending (gui_multiline): Shift+Enter adds a line, Enter
# sends them as one batch; a line too long for one PRIVMSG is split with
# draft/multiline-concat. With the setting off nothing changes.
# usage: tests/ux/multiline.sh path/to/nexchat     (needs Xvfb + XTest)
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:99}
tmp=$(mktemp -d)
app=; ircd=; xv=
trap 'kill $app $ircd $xv 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"' EXIT
start_xvfb "$xvfb" "$disp"

k() { DISPLAY=$disp python3 "$here/xkey.py" --delay 0.15 --at 450,580 "$@"; }
t() { DISPLAY=$disp python3 "$here/xkey.py" --delay 0.02 --at 450,580 --type "$1"; }

start () # extra hexchat.conf lines
{
	rm -rf "$tmp/cfg"; mkdir -p "$tmp/cfg"; rm -f "$tmp/port"; : > "$tmp/server.log"
	printf 'net_throttle = 0\nirc_nick1 = ml\ngui_slist_skip = 1\ngui_win_left = 0\ngui_win_top = 0\ngui_win_width = 900\ngui_win_height = 600\n%s\n' "$1" > "$tmp/cfg/hexchat.conf"
	python3 "$here/../fake_ircd.py" --timeout 60 --log "$tmp/server.log" --portfile "$tmp/port" \
		--caps "batch message-tags draft/multiline=max-bytes=4096,max-lines=24" 2>/dev/null &
	ircd=$!
	i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
	DISPLAY=$disp NEXCHAT_TABS_DUMP=1 "$bin" -a -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/#t" >"$tmp/log" 2>&1 &
	app=$!
	wait_window "$disp"
	wait_for grep -q '^tab: #t' "$tmp/log"
	sleep 0.5
}
stop () { kill $app $ircd 2>/dev/null || true; wait $app $ircd 2>/dev/null || true; }

fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }

start 'gui_multiline = 1'
t "one"; k shift+Return; t "two"; k shift+Return; t "//three"; k Return
long=$(python3 -c 'print("x" * 600)')
t "short"; k shift+Return; t "$long"; k Return
sleep 1.5
stop
b=$(grep -c '^BATCH +[^ ]* draft/multiline #t' "$tmp/server.log" || true)
[ "$b" -eq 2 ] && ok "two messages, two batches" || bad "$b batches"
grep -q '^@batch=[^ ]* PRIVMSG #t :one$' "$tmp/server.log" &&
grep -q '^@batch=[^ ]* PRIVMSG #t :two$' "$tmp/server.log" &&
grep -q '^@batch=[^ ]* PRIVMSG #t :/three$' "$tmp/server.log" && ok "Shift+Enter lines in the batch (// says /)" || bad "batch lines"
grep -q '^@batch=[^ ;]*;draft/multiline-concat PRIVMSG #t :x' "$tmp/server.log" && ok "a long line goes on with multiline-concat" || bad "no concat"
grep -q '^PRIVMSG #t :one' "$tmp/server.log" && bad "also sent outside the batch" || ok "nothing outside the batch"
grep -c '^BATCH -' "$tmp/server.log" | grep -qx 2 && ok "batches closed" || bad "BATCH - count"

start ''
t "solo"; k shift+Return; t "more"; k Return
sleep 1
stop
grep -q '^PRIVMSG #t :solomore$' "$tmp/server.log" && ok "off: Shift+Enter adds no line (as before)" || bad "off: not one plain line"
grep -q '^CAP REQ .*draft/multiline' "$tmp/server.log" && bad "off: cap requested" || ok "off: cap not asked for"

[ $fail -eq 0 ] || { cat "$tmp/server.log"; exit 1; }
echo "multiline: ok"

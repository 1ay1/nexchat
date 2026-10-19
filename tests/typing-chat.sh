#!/bin/sh
# Typing in the chat itself (gui_typing_chat): "bob started typing" when he
# starts, "bob stopped typing" when he says done or goes quiet (6 s), and
# nothing extra when his line comes (the line says it). Both go in the
# channel's log too. "carol is typing..." stays under the input box.
# usage: tests/typing-chat.sh path/to/nexchat-text
set -eu
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
ircd=
trap 'kill $ircd 2>/dev/null || true; rm -rf "$tmp"' EXIT

# bob: starts, stops (done). carol: starts, then her line comes.
# dave: starts and goes quiet (stale after 6 s).
cat > "$tmp/script" <<'EOF'
@+typing=active :bob!b@h TAGMSG #t
@+typing=active :bob!b@h TAGMSG #t
@+typing=done :bob!b@h TAGMSG #t
@+typing=active :carol!c@h TAGMSG #t
:carol!c@h PRIVMSG #t :hello all
@+typing=active :dave!d@h TAGMSG #t
EOF
python3 "$here/fake_ircd.py" --timeout 40 --log "$tmp/server.log" --portfile "$tmp/port" \
	--caps "message-tags" --script "$tmp/script" --script-after '#t' &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done

run () # extra hexchat.conf
{
	rm -rf "$tmp/cfg"; mkdir -p "$tmp/cfg"
	printf 'net_throttle = 0\nirc_nick1 = ty\nirc_logging = 1\ngui_typing_show = 1\n%s\n' "$1" > "$tmp/cfg/hexchat.conf"
	{
		sleep 1; echo "/join #t"; sleep 10; echo "/quit"; sleep 0.5
	} | timeout 25 "$bin" -n -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/" 2>&1 |
		sed 's/\x1b\[[0-9;]*m//g' > "$tmp/out" || true
}

fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }
count() { grep -c -- "$1" "$2" || true; }

run 'gui_typing_chat = 1'
[ "$(count 'bob started typing' "$tmp/out")" -eq 1 ] && ok "bob started typing, said once (not once per notice)" || bad "bob started"
[ "$(count 'bob stopped typing' "$tmp/out")" -eq 1 ] && ok "bob stopped typing (he said done)" || bad "bob stopped"
[ "$(count 'carol started typing' "$tmp/out")" -eq 1 ] && ok "carol started typing" || bad "carol started"
[ "$(count 'carol stopped typing' "$tmp/out")" -eq 0 ] && ok "no 'stopped' for carol: her line came" || bad "carol stopped said"
grep -q 'dave started typing' "$tmp/out" && grep -q 'dave stopped typing' "$tmp/out" &&
	ok "dave went quiet: stopped after 6 s" || bad "dave"
log=$(find "$tmp/cfg/logs" -type f -name '*#t*' | head -1)
if [ -n "$log" ]; then
	grep -q 'bob started typing' "$log" && grep -q 'bob stopped typing' "$log" && ok "in the log too" || bad "not in the log"
else
	bad "no log file"
fi

run ''
grep -q 'started typing\|stopped typing' "$tmp/out" && bad "lines in the chat with the setting off" || ok "off by default: nothing in the chat"

[ $fail -eq 0 ] || { cat "$tmp/out"; exit 1; }
echo "typing-chat: ok"
exit 0

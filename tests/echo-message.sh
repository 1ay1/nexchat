#!/bin/sh
# IRCv3 echo-message: our own lines are shown once, as the server sent them
# back (not once locally and again from the echo); /me and /msg too.
# usage: tests/echo-message.sh path/to/nexchat-text
set -eu
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'kill $ircd 2>/dev/null || true; rm -rf "$tmp"' EXIT

python3 "$here/fake_ircd.py" --log "$tmp/server.log" --portfile "$tmp/port" --caps "echo-message" &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = echo\n' > "$tmp/cfg/hexchat.conf"
{
	sleep 1; echo "/join #t"; sleep 1
	echo "plain-hello"; echo "/me waves-hi"; echo "/msg #t via-msg"
	sleep 1; echo "/quit bye"; sleep 1
} | timeout 20 "$bin" -n -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/" > "$tmp/client.log" 2>&1 || true

fail=0
once() {
	n=$(grep -c -- "$1" "$tmp/client.log" || true)
	if [ "$n" -eq 1 ]; then echo "ok:   $2"; else echo "FAIL: $2 (shown $n times)"; fail=1; fi
}
grep -q "^CAP REQ .*echo-message" "$tmp/server.log" && echo "ok:   echo-message requested" || { echo "FAIL: not requested"; fail=1; }
once 'plain-hello' 'typed line shown once'
once 'waves-hi' '/me shown once'
once 'via-msg' '/msg to an open channel shown once'
grep -q '<echo>.*plain-hello\|echo.*plain-hello' "$tmp/client.log" || { echo "FAIL: not shown as ours"; fail=1; }
if [ $fail -ne 0 ]; then tail -20 "$tmp/client.log"; exit 1; fi
echo "echo-message: ok"

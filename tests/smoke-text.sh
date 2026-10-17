#!/bin/sh
# Smoke test: connect the text frontend to the fake ircd, join, talk, quit.
# usage: tests/smoke-text.sh path/to/hexchat-text
set -eu
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'kill $ircd 2>/dev/null || true; rm -rf "$tmp"' EXIT

python3 "$here/fake_ircd.py" --log "$tmp/server.log" --portfile "$tmp/port" &
ircd=$!
i=0
while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
port=$(cat "$tmp/port")

mkdir -p "$tmp/cfg"
# throttled QUIT gets flushed before it's sent (upstream behaviour), so turn throttling off
printf 'net_throttle = 0\nirc_nick1 = smoke\n' > "$tmp/cfg/hexchat.conf"
# macOS has no timeout(1): a shell watchdog does the same
run_limited () {
	if command -v timeout >/dev/null; then
		timeout "$@"
	else
		secs=$1; shift
		# a backgrounded job gets /dev/null as stdin: hand it ours
		"$@" <&0 &
		pid=$!
		( sleep "$secs"; kill "$pid" 2>/dev/null ) &
		wd=$!
		wait "$pid" || true
		kill "$wd" 2>/dev/null || true
	fi
}
{
	sleep 1
	echo "/join #smoke"
	sleep 1
	echo "hello-from-client"
	sleep 1
	echo "/quit bye"
	sleep 1
} | run_limited 20 "$bin" -n -d "$tmp/cfg" "irc://127.0.0.1:-$port/" > "$tmp/client.log" 2>&1 || true

fail=0
check() {
	if ! grep -q -- "$2" "$1"; then
		echo "FAIL: '$2' not in $(basename "$1")"
		fail=1
	fi
}
check "$tmp/server.log" "^NICK "
check "$tmp/server.log" "^JOIN #smoke"
check "$tmp/server.log" "^PRIVMSG #smoke :hello-from-client"
check "$tmp/server.log" "^QUIT"
check "$tmp/client.log" "pong-reply"

if [ $fail -ne 0 ]; then
	echo "--- server.log"; cat "$tmp/server.log"
	echo "--- client.log"; tail -40 "$tmp/client.log"
	exit 1
fi
echo "smoke-text: ok"

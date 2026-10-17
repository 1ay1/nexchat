#!/bin/sh
# irc_smart_filter: joins/parts/quits only of people who spoke recently.
# usage: tests/smart-filter.sh path/to/nexchat-text
set -eu
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'kill $ircd 2>/dev/null || true; rm -rf "$tmp"' EXIT

cat > "$tmp/script" <<'EOF'
:bob!b@h PRIVMSG #t :bob-says-hi
:bob!b@h PART #t :bob-leaves
:carol!c@h JOIN #t
:carol!c@h PART #t :carol-leaves
:dave!d@h JOIN #t
:dave!d@h PRIVMSG #t :dave-says-hi
:dave!d@h QUIT :dave-quits
EOF

python3 "$here/fake_ircd.py" --log "$tmp/server.log" --portfile "$tmp/port" --script "$tmp/script" &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done

mkdir -p "$tmp/cfg"
printf '%s\n' "net_throttle = 0" "irc_nick1 = smart" "irc_smart_filter = 10" > "$tmp/cfg/hexchat.conf"
{
	sleep 1; echo "/join #t"; sleep 3; echo "/quit bye"; sleep 1
} | timeout 20 "$bin" -n -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/" > "$tmp/client.log" 2>&1 || true

fail=0
want () { grep -q -- "$1" "$tmp/client.log" || { echo "FAIL: '$1' should show"; fail=1; }; }
hide () { ! grep -q -- "$1" "$tmp/client.log" || { echo "FAIL: '$1' should be hidden"; fail=1; }; }
want bob-says-hi
want bob-leaves
want dave-says-hi
want dave-quits
hide carol
hide "dave.*has joined"
if [ $fail -ne 0 ]; then tail -20 "$tmp/client.log"; exit 1; fi
echo ok

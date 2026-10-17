#!/bin/sh
# IRCv3 batch: a netsplit batch turns its QUITs into one line per channel,
# and a netjoin its JOINs; lines outside a batch print as before.
# usage: tests/batch.sh path/to/nexchat-text
set -eu
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'kill $ircd 2>/dev/null || true; rm -rf "$tmp"' EXIT

cat > "$tmp/script" <<'EOF'
:fake 353 {nick} = #t :alice bob carol dave
:fake 366 {nick} #t :end
:fake BATCH +s1 netsplit hub.example leaf.example
@batch=s1 :alice!a@h QUIT :hub.example leaf.example
@batch=s1 :bob!b@h QUIT :hub.example leaf.example
@batch=s1 :carol!c@h QUIT :hub.example leaf.example
:fake BATCH -s1
:dave!d@h QUIT :plain-quit-outside
:fake BATCH +j1 netjoin hub.example leaf.example
@batch=j1 :alice!a@h JOIN #t
@batch=j1 :bob!b@h JOIN #t
:fake BATCH -j1
EOF
python3 "$here/fake_ircd.py" --log "$tmp/server.log" --portfile "$tmp/port" --caps "batch" --script "$tmp/script" &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = batch\n' > "$tmp/cfg/hexchat.conf"
{
	sleep 1; echo "/join #t"; sleep 2; echo "/quit bye"; sleep 1
} | timeout 20 "$bin" -n -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/" > "$tmp/client.log" 2>&1 || true

fail=0
want() { grep -q -- "$1" "$tmp/client.log" && echo "ok:   $2" || { echo "FAIL: $2"; fail=1; }; }
none() { if grep -q -- "$1" "$tmp/client.log"; then echo "FAIL: $2"; fail=1; else echo "ok:   $2"; fi; }
grep -q "^CAP REQ .*batch" "$tmp/server.log" && echo "ok:   batch requested" || { echo "FAIL: batch not requested"; fail=1; }
want 'Netsplit (hub.example leaf.example): alice, bob, carol' 'netsplit summarised in one line'
none 'alice has quit' 'no per-person quit lines in the batch'
want 'dave' 'a quit outside the batch still shows'
want 'Netsplit over (hub.example leaf.example), back: alice, bob' 'netjoin summarised'
none 'alice.*has joined' 'no per-person join lines in the batch'
if [ $fail -ne 0 ]; then tail -20 "$tmp/client.log"; exit 1; fi
echo "batch: ok"

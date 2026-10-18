#!/bin/sh
# draft/multiline (behind gui_multiline): a batch from the server shows as
# its lines (concat joins without a break), with the batch's msgid on the
# first; the cap is asked for only when the setting is on.
# usage: tests/multiline.sh path/to/nexchat-text
set -eu
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
ircd=
trap 'kill $ircd 2>/dev/null || true; rm -rf "$tmp"' EXIT

cat > "$tmp/script" <<'EOF'
@msgid=b1 :alice!a@h BATCH +ml draft/multiline #t
@batch=ml :alice!a@h PRIVMSG #t :first line
@batch=ml :alice!a@h PRIVMSG #t :second line, which is long and
@batch=ml;draft/multiline-concat :alice!a@h PRIVMSG #t : goes on here
@batch=ml :alice!a@h PRIVMSG #t :
@batch=ml :alice!a@h PRIVMSG #t :after a blank
:alice!a@h BATCH -ml
@msgid=r1;+draft/reply=b1 :bob!b@h PRIVMSG #t :nice
EOF

start_ircd () {
	rm -f "$tmp/port"; : > "$tmp/server.log"
	python3 "$here/fake_ircd.py" --log "$tmp/server.log" --portfile "$tmp/port" \
		--caps "batch message-tags draft/multiline=max-bytes=4096,max-lines=24" \
		--script "$tmp/script" --script-after '#t' &
	ircd=$!
	i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
}

run () # extra hexchat.conf lines
{
	rm -rf "$tmp/cfg"; mkdir -p "$tmp/cfg"
	printf 'net_throttle = 0\nirc_nick1 = ml\n%s\n' "$1" > "$tmp/cfg/hexchat.conf"
	{
		sleep 1; echo "/join #t"; sleep 2
		echo "/quit bye"; sleep 1
	} | timeout 20 "$bin" -n -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/" 2>&1 |
		sed 's/\x1b\[[0-9;]*m//g' > "$tmp/client.log" || true
}

fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }

start_ircd
run 'gui_multiline = 1
gui_replies = 1'
grep -q '^CAP REQ .*draft/multiline' "$tmp/server.log" && ok "cap asked for when on" || bad "cap not requested"
grep -q '<alice>.*first line' "$tmp/client.log" && ok "first line shown" || bad "first line"
grep -q '<alice>.*second line, which is long and goes on here' "$tmp/client.log" && ok "concat joins without a break" || bad "concat"
grep -q '<alice>.*after a blank' "$tmp/client.log" && ok "line after a blank one" || bad "after blank"
n=$(grep -c '<alice>' "$tmp/client.log" || true)
[ "$n" -eq 3 ] && ok "blank line left out (3 lines)" || bad "$n alice lines, want 3"
grep -q 'replying to alice: first line' "$tmp/client.log" && ok "batch msgid is the first line's" || bad "reply to batch msgid"

kill $ircd 2>/dev/null || true; wait $ircd 2>/dev/null || true
start_ircd
run ''
if grep -q '^CAP REQ .*draft/multiline' "$tmp/server.log"; then bad "cap requested with the setting off"; else ok "off by default: not asked for"; fi

[ $fail -eq 0 ] || { cat "$tmp/client.log"; cat "$tmp/server.log"; exit 1; }
echo "multiline: ok"

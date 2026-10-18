#!/bin/sh
# +draft/reply (behind gui_replies): a reply shows what it answers above
# it; /reply answers that person's last line with the tag, or falls back
# to "nick, text" (the completion suffix) when there's nothing to point at.
# usage: tests/reply.sh path/to/nexchat-text
set -eu
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
ircd=
trap 'kill $ircd 2>/dev/null || true; rm -rf "$tmp"' EXIT

cat > "$tmp/script" <<'EOF'
@msgid=m1 :alice!a@h PRIVMSG #t :is the release out yet?
@msgid=m2;+draft/reply=m1 :bob!b@h PRIVMSG #t :yes, this morning
@msgid=m3;+draft/reply=gone :carol!c@h PRIVMSG #t :me too
EOF
python3 "$here/fake_ircd.py" --log "$tmp/server.log" --portfile "$tmp/port" \
	--caps "message-tags" --script "$tmp/script" --script-after '#t' &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done

run () # extra hexchat.conf lines
{
	rm -rf "$tmp/cfg"; mkdir -p "$tmp/cfg"
	printf 'net_throttle = 0\nirc_nick1 = rep\n%s\n' "$1" > "$tmp/cfg/hexchat.conf"
	{
		sleep 1; echo "/join #t"; sleep 1.5
		echo "/reply alice thanks"; echo "/reply nobody hi"
		sleep 1; echo "/quit bye"; sleep 1
	} | timeout 20 "$bin" -n -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/" > "$tmp/client.log" 2>&1 || true
}

fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }

run 'gui_replies = 1'
grep -q 'replying to alice: is the release out yet?' "$tmp/client.log" && ok "reply shows what it answers" || bad "no quote line"
grep -q 'replying to an earlier message' "$tmp/client.log" && ok "unknown msgid: still marked as a reply" || bad "unknown msgid"
grep -q '^@+draft/reply=m1 PRIVMSG #t :thanks' "$tmp/server.log" && ok "/reply sends the tag" || bad "/reply tag not sent"
grep -q '^PRIVMSG #t :nobody, hi' "$tmp/server.log" && ok "/reply with nothing to answer: nick, text" || bad "fallback"

kill $ircd 2>/dev/null || true; wait $ircd 2>/dev/null || true
: > "$tmp/server.log"; rm -f "$tmp/port"
python3 "$here/fake_ircd.py" --log "$tmp/server.log" --portfile "$tmp/port" \
	--caps "message-tags" --script "$tmp/script" --script-after '#t' &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
run ''
if grep -q 'replying to' "$tmp/client.log"; then bad "shown with gui_replies off"; else ok "off by default: nothing shown"; fi

[ $fail -eq 0 ] || { cat "$tmp/client.log"; cat "$tmp/server.log"; exit 1; }
echo "reply: ok"

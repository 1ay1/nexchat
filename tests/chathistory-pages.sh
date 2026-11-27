#!/bin/sh
# Catching up after being away: everything missed comes in, however many
# messages, not just the first page. On rejoining, the client asks
# CHATHISTORY AFTER the last line it has, as many as the server allows
# (ISUPPORT CHATHISTORY=n), and keeps asking from the last msgid while
# pages come back full; one "N messages you missed" line at the end.
# usage: tests/chathistory-pages.sh path/to/nexchat-text
set -eu
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
ircd=
trap 'kill $ircd 2>/dev/null || true; [ -n "${KEEP:-}" ] && cp -r "$tmp" "$KEEP"; rm -rf "$tmp"' EXIT

# a live line, so the channel has a newest message to ask after
printf ':bob!b@h PRIVMSG #t :live-before\n' > "$tmp/script"
python3 "$here/fake_ircd.py" --log "$tmp/server.log" --portfile "$tmp/port" \
	--caps "batch server-time message-tags draft/chathistory" --isupport "CHATHISTORY=40" \
	--after-lines 250 --script "$tmp/script" &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = hist\ntext_replay = 0\n' > "$tmp/cfg/hexchat.conf"
{
	sleep 1; echo "/join #t"; sleep 2; echo "/part #t"; sleep 1; echo "/join #t"; sleep 4; echo "/quit bye"; sleep 1
} | timeout 25 "$bin" -n -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/" > "$tmp/client.log" 2>&1 || true

fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }
grep -q '^CHATHISTORY AFTER #t timestamp=[^ ]* 40$' "$tmp/server.log" && ok "first page: after the newest line, the server's page size (40)" \
	|| bad "first page: $(grep '^CHATHISTORY AFTER' "$tmp/server.log" | head -1)"
n=$(grep -c '^CHATHISTORY AFTER #t' "$tmp/server.log" || true)
[ "$n" -eq 7 ] && ok "7 pages for 250 messages (6 full, then the rest)" || bad "$n AFTER requests, want 7"
grep -q '^CHATHISTORY AFTER #t msgid=m39 40$' "$tmp/server.log" && ok "the next page from the last msgid" \
	|| bad "next page: $(grep '^CHATHISTORY AFTER' "$tmp/server.log" | sed -n 2p)"
for k in 0 39 40 199 249; do
	grep -q "missed-$k$(printf '\r')*\$" "$tmp/client.log" || bad "missed-$k not shown"
done
[ $fail -eq 0 ] && ok "all 250 shown, first to last"
d=$(grep -c 'missed-' "$tmp/client.log" || true)
[ "$d" -eq 250 ] && ok "each once" || bad "$d missed lines shown, want 250"
# the first join's own (LATEST) history has its marker; the rejoin, one
w=$(sed -n '/you missed, up to here/,$p' "$tmp/client.log" | sed 1d | grep -c 'While you were away' || true)
[ "$w" -eq 1 ] && ok "one 'While you were away' for the catch-up" || bad "$w 'While you were away' markers on rejoining"
grep -q '250 messages you missed, up to here' "$tmp/client.log" && ok "one end line counting all 250" \
	|| bad "end line: $(grep 'you missed' "$tmp/client.log" | tr '\n' '|')"

if [ $fail -ne 0 ]; then echo "--- server"; grep -v '^PING\|^PONG' "$tmp/server.log" | tail -20; echo "--- client"; grep -v 'missed-' "$tmp/client.log" | tail -20; exit 1; fi
echo "chathistory-pages: ok"

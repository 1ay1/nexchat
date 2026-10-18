#!/bin/sh
# IRCv3 chathistory: on joining a channel the client asks for what was
# missed; the lines come back in a batch and are shown between markers,
# but not logged again, not counted unread and a mention in them doesn't
# alert. Live lines afterwards behave as usual.
# usage: tests/chathistory.sh path/to/nexchat-text
set -eu
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'kill $ircd 2>/dev/null || true; rm -rf "$tmp"' EXIT

# a live line after the history, so logging of live lines is checked too
cat > "$tmp/script" <<'EOF'
:bob!b@h PRIVMSG #t :live-line-after
EOF
python3 "$here/fake_ircd.py" --log "$tmp/server.log" --portfile "$tmp/port" \
	--caps "batch server-time draft/chathistory" --script "$tmp/script" &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = hist\nirc_logging = 1\ntext_replay = 0\n' > "$tmp/cfg/hexchat.conf"
{
	sleep 1; echo "/join #t"; sleep 2; echo "/query alice"; sleep 2; echo "/quit bye"; sleep 1
} | timeout 20 "$bin" -n -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/" > "$tmp/client.log" 2>&1 || true

fail=0
want() { grep -q -- "$1" "$2" && echo "ok:   $3" || { echo "FAIL: $3"; fail=1; }; }
none() { if grep -q -- "$1" "$2"; then echo "FAIL: $3"; fail=1; else echo "ok:   $3"; fi; }
want '^CAP REQ .*draft/chathistory' "$tmp/server.log" 'chathistory requested'
want '^CHATHISTORY LATEST #t \* 100' "$tmp/server.log" 'asks for the latest 100 on join (nothing seen before)'
want 'While you were away' "$tmp/client.log" 'history marker shown'
want 'history-line-one' "$tmp/client.log" 'history lines shown'
want 'history-line-three' "$tmp/client.log" 'all of them'
want '3 messages you missed, up to here' "$tmp/client.log" 'end marker counts them'
want '^CHATHISTORY LATEST alice \* 100' "$tmp/server.log" '/query alice asks for your history with her'
log=$(find "$tmp/cfg/logs" -type f -name '*.log' 2>/dev/null | xargs grep -l 'live-line-after' 2>/dev/null | head -1)
if [ -z "$log" ]; then log=$(find "$tmp/cfg/logs" -type f 2>/dev/null | grep -i '#t' | head -1); fi
if [ -n "$log" ]; then
	none 'history-line-one' "$log" 'history not written to the log again'
	want 'live-line-after' "$log" 'live lines still logged'
else
	echo "FAIL: no log file"; fail=1
fi
if [ $fail -ne 0 ]; then echo "--- server"; cat "$tmp/server.log"; echo "--- client"; tail -20 "$tmp/client.log"; exit 1; fi
echo "chathistory: ok"

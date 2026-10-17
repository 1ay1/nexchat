#!/bin/sh
# What a hostile server/user can send: a DCC SEND whose file name and nick
# try to leave the download dir, and a tagged message.
# usage: tests/hostile-text.sh path/to/nexchat-text
set -eu
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'kill $ircd $peer 2>/dev/null || true; rm -rf "$tmp"' EXIT
ircd=; peer=

# a DCC peer that sends 5 bytes, so an accepted file really gets written
python3 - "$tmp/peerport" <<'EOF' &
import socket, sys
s = socket.socket(); s.bind(("127.0.0.1", 0)); s.listen(1)
open(sys.argv[1], "w").write(str(s.getsockname()[1]))
s.settimeout(20)
try:
    c, _ = s.accept(); c.sendall(b"pwned"); c.recv(16); c.close()
except Exception:
    pass
EOF
peer=$!
i=0; while [ ! -s "$tmp/peerport" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
pport=$(cat "$tmp/peerport")

# 2130706433 = 127.0.0.1
c=$(printf '\001')
rlo=$(printf '\342\200\256')
cat > "$tmp/script" <<EOF
:../../evil!e@h PRIVMSG {nick} :${c}DCC SEND ../../../.escape 2130706433 $pport 5${c}
@solanum.chat/identified;time=2026-01-01T00:00:00.000Z :bob!b@h PRIVMSG #t :tagged-hello
:mal${rlo}!m@h PRIVMSG #t :bidi-tail
EOF

python3 "$here/fake_ircd.py" --log "$tmp/server.log" --portfile "$tmp/port" --script "$tmp/script" &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done

mkdir -p "$tmp/cfg" "$tmp/dl/sub"
printf '%s\n' "net_throttle = 0" "irc_nick1 = hostile" "dcc_auto_recv = 2" \
	"dcc_save_nick = 1" "dcc_dir = $tmp/dl/sub" "dcc_completed_dir = $tmp/dl/sub" \
	> "$tmp/cfg/hexchat.conf"
{
	sleep 1; echo "/join #t"; sleep 4; echo "/quit bye"; sleep 1
} | timeout 20 "$bin" -n -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/" > "$tmp/client.log" 2>&1 || true

fail=0
# nothing may land outside dl/sub, and nothing named with a leading dot
outside=$(find "$tmp" -path "$tmp/dl/sub" -prune -o -path "$tmp/cfg" -prune -o -type f -print \
	| grep -v -e "/server.log$" -e "/client.log$" -e "/script$" -e "/port$" -e "/peerport$" || true)
if [ -n "$outside" ]; then echo "FAIL: written outside the DCC dir: $outside"; fail=1; fi
if ls -A "$tmp/dl/sub" | grep -q '^\.'; then echo "FAIL: dot file in the DCC dir"; fail=1; fi
if ! ls "$tmp/dl/sub" | grep -q escape; then echo "FAIL: the file wasn't received at all"; fail=1; fi
grep -q "tagged-hello" "$tmp/client.log" || { echo "FAIL: tagged message not shown"; fail=1; }
# a nick ending in RIGHT-TO-LEFT OVERRIDE is wrapped in FSI..PDI, so it
# can't reverse the message after it
fsi=$(printf '\342\201\250'); pdi=$(printf '\342\201\251')
grep -q "${fsi}mal${rlo}${pdi}.*bidi-tail" "$tmp/client.log" || { echo "FAIL: bidi nick not isolated"; grep -a bidi-tail "$tmp/client.log" | od -c | head; fail=1; }

if [ $fail -ne 0 ]; then
	echo "--- dl:"; ls -laR "$tmp/dl"
	echo "--- client.log"; tail -30 "$tmp/client.log"
	exit 1
fi

# the same offer (from 127.0.0.1) as if the server were a public one:
# auto-accept must ask instead (fe-text can't ask, so nothing arrives)
rm -rf "$tmp/dl/sub"/*
python3 - "$tmp/peerport" <<'EOF' &
import socket, sys
s = socket.socket(); s.bind(("127.0.0.1", 0)); s.listen(1)
open(sys.argv[1], "w").write(str(s.getsockname()[1]))
s.settimeout(10)
try:
    c, _ = s.accept(); c.sendall(b"pwned"); c.recv(16); c.close()
except Exception:
    pass
EOF
peer=$!
sleep 0.5
pport=$(cat "$tmp/peerport")
printf ':peer!p@h PRIVMSG {nick} :%sDCC SEND lan.txt 2130706433 %s 5%s\n' "$c" "$pport" "$c" > "$tmp/script"
rm -f "$tmp/port"
python3 "$here/fake_ircd.py" --log "$tmp/server.log" --portfile "$tmp/port" --script "$tmp/script" &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
{
	sleep 1; echo "/join #t"; sleep 3; echo "/quit bye"; sleep 1
} | NEXCHAT_DCC_PUBLIC_SERVER=1 timeout 20 "$bin" -n -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/" > "$tmp/client.log" 2>&1 || true
if ls "$tmp/dl/sub" | grep -q lan; then
	echo "FAIL: an offer to a private address was taken without asking"; exit 1
fi
echo "ok:   offer to a private address from a public server isn't auto-accepted"
echo "hostile: ok"

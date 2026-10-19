#!/bin/sh
# draft/message-redaction against the real Ergo (tests/bnc/lab.sh, with
# allow-individual-delete): someone deleting a message is shown with what
# it was; /delete takes back your own last one. Ergo only lets you delete
# your own when logged in, so both have accounts.
# usage: tests/redact.sh path/to/nexchat-text   (77 = bouncers not fetched)
set -eu
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
. "$here/bnc/lab.sh"
bnc_have || { echo "redact: not fetched (tests/bnc/fetch.sh), skipped"; exit 77; }
tmp=$(mktemp -d)
trap 'st=$?; bnc_stop; rm -rf "$tmp"; exit $st' EXIT
bnc_start "$tmp/lab"

fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }
has() { grep -q -- "$1" "$tmp/out" && ok "$2" || bad "$2"; }

cat > "$tmp/register.py" <<'EOF'
import socket, sys, time
# ("ayush" is ZNC's nick on this network in the lab)
for nick in ("bob", "me"):
    s = socket.create_connection(("127.0.0.1", int(sys.argv[1])))
    def w(l): s.sendall((l + "\r\n").encode())
    w("NICK " + nick); w("USER %s 0 * :x" % nick)
    s.settimeout(20); buf = b""
    while b" 001 " not in buf:
        buf += s.recv(4096)
    w("PRIVMSG NickServ :REGISTER pw12345 *")
    while b"Account created" not in buf:
        buf += s.recv(4096)
    w("QUIT"); time.sleep(0.2)
EOF
python3 "$tmp/register.py" "$ERGO_PORT" || { echo "FAIL: couldn't register accounts"; exit 1; }

# bob: logs in, joins, says something, deletes it with a reason
cat > "$tmp/bob.py" <<'EOF'
import socket, sys, time
s = socket.create_connection(("127.0.0.1", int(sys.argv[1]))); f = s.makefile("rwb")
def w(l): f.write((l + "\r\n").encode()); f.flush()
w("CAP REQ :message-tags echo-message draft/message-redaction")
w("PASS bob:pw12345"); w("NICK bob"); w("USER bob 0 * :b"); w("CAP END")
time.sleep(1); w("JOIN #r"); time.sleep(3)
w("PRIVMSG #r :oops wrong window"); time.sleep(1)
buf = b""
s.settimeout(1)
try:
    while True:
        d = s.recv(4096)
        if not d: break
        buf += d
except Exception: pass
mid = [l for l in buf.decode().splitlines() if "oops" in l][0].split("msgid=")[1].split(";")[0].split(" ")[0]
time.sleep(0.5); w("REDACT #r %s :meant for alice" % mid); time.sleep(6); w("QUIT")
EOF
python3 "$tmp/bob.py" "$ERGO_PORT" > /dev/null 2>&1 &
bob=$!

mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = me\n' > "$tmp/cfg/hexchat.conf"
# logged in with SASL, joins #r
printf 'v=2.16.2\n\nN=ergo\nI=me\nU=me\nP=pw12345\nL=6\nF=0\nD=0\nS=127.0.0.1/%s\nJ=#r\n\n' "$ERGO_PORT" > "$tmp/cfg/servlist.conf"
{
	sleep 1; echo "/join #r"; sleep 5
	echo "keep this"; sleep 0.5
	echo "typo here"; sleep 1
	echo "/delete"; sleep 1.5
	echo "/delete"; sleep 1
	echo "/quit"; sleep 0.5
} | timeout 25 "$bin" -n -d "$tmp/cfg" "irc://ergo" 2>&1 | sed 's/\x1b\[[0-9;]*m//g' > "$tmp/out" || true
wait $bob 2>/dev/null || true

grep -q 'Capabilities acknowledged:.*draft/message-redaction' "$tmp/out" && ok "asked for" || bad "not requested"
has '<bob> oops wrong window' "bob's line shown first"
has 'bob deleted "oops wrong window" (meant for alice)' "a deletion says what and why"
has 'me deleted "typo here"' "/delete takes back your last line"
has 'me deleted "keep this"' "a second /delete takes the one before"

[ $fail -eq 0 ] || { cat "$tmp/out"; exit 1; }
echo "redact: ok"
exit 0

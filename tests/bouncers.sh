#!/bin/sh
# ZNC and Ergo, the real ones (tests/bnc/lab.sh): ZNC detected, its networks
# managed through *controlpanel and its playback marked; Ergo's always-on
# (draft/persistence) turned on; a plain server explained.
# usage: tests/bouncers.sh path/to/nexchat-text   (77 = bouncers not fetched)
set -eu
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
. "$here/bnc/lab.sh"
bnc_have || { echo "bouncers: not fetched (tests/bnc/fetch.sh), skipped"; exit 77; }
tmp=$(mktemp -d)
trap 'st=$?; bnc_stop; rm -rf "$tmp"; exit $st' EXIT
bnc_start "$tmp/lab"

fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }
has() { grep -q -- "$1" "$tmp/out" && ok "$2" || bad "$2"; }

# run nexchat-text against a server with a network entry; stdin lines with
# a pause before each
run () # name host/port user pass logintype extra-flags "lines"...
{
	name=$1 hostport=$2 user=$3 pass=$4 login=$5 flags=$6; shift 6
	if [ -z "${keepcfg:-}" ] || [ ! -f "$tmp/cfg/servlist.conf" ]; then
	rm -rf "$tmp/cfg"; mkdir -p "$tmp/cfg"
	printf 'net_throttle = 0\nirc_nick1 = ayush\nirc_user_name = ayush\n' > "$tmp/cfg/hexchat.conf"
	printf 'v=2.16.2\n\nN=%s\nU=%s\nP=%s\nL=%s\nF=%s\nD=0\nS=%s\n\n' \
		"$name" "$user" "$pass" "$login" "$flags" "$hostport" > "$tmp/cfg/servlist.conf"
	fi
	{
		sleep 2
		for l in "$@"; do echo "$l"; sleep 1.2; done
		echo "/quit"; sleep 0.5
	} | timeout 40 "$bin" -n -d "$tmp/cfg" "irc://$name" 2>&1 | sed 's/\x1b\[[0-9;]*m//g' > "$tmp/out" || true
}

# F=0: no global user info, no TLS. L=7 server password (PASS), L=6 SASL
# --- ZNC: detected, its network listed, add/remove through *controlpanel
run znc "127.0.0.1/$ZNC_PORT" "ayush/ergo" secret123 7 0 \
	"/bouncer" \
	"/bouncer add second 127.0.0.1:$ERGO_PORT -notls" \
	"/bouncer" \
	"/bouncer add second 127.0.0.1:$ERGO_PORT -notls" \
	"/bouncer del second" \
	"/bouncer"
has 'Connected through ZNC' "znc: detected"
has '  ergo  ' "znc: its network listed"
has 'second added' "znc: add said so"
has '  second  ' "znc: the new network listed"
has 'ZNC said no: User ayush already has a network with the name second' "znc: a failed add says why"
has 'Removed from the bouncer' "znc: remove said so"
grep -q '\*controlpanel' "$tmp/out" && bad "znc: module chatter shown" || ok "znc: no *controlpanel chatter in the window"
cp "$tmp/out" "$tmp/znc.out"

# --- ZNC: what was said while away is replayed, between the markers
# (ZNC sits in #mine for us; bob talks there while we're not connected)
python3 - "$ZNC_PORT" <<'EOF'
import socket, sys, time
s = socket.create_connection(("127.0.0.1", int(sys.argv[1]))); f = s.makefile("rwb")
def w(l): f.write((l + "\r\n").encode()); f.flush()
w("PASS secret123"); w("NICK ayush"); w("USER ayush/ergo 0 * :x"); time.sleep(1)
w("JOIN #mine"); time.sleep(1); w("QUIT"); time.sleep(0.3)
EOF
bnc_say bob "#mine" "missed-one" "missed-two ayush look"
run znc "127.0.0.1/$ZNC_PORT" "ayush/ergo" secret123 7 0 "/echo done"
has 'While you were away' "znc: playback marked"
has 'missed-one' "znc: missed lines shown"
has '2 messages you missed, up to here' "znc: and counted"
grep -q 'missed-two ayush look' "$tmp/out" && ok "znc: a mention in it is shown" || bad "znc: mention"

# --- Ergo: a network that can keep you online; turn it on
python3 - "$ERGO_PORT" <<'EOF'
import socket, sys, time
s = socket.create_connection(("127.0.0.1", int(sys.argv[1]))); f = s.makefile("rwb")
def w(l): f.write((l + "\r\n").encode()); f.flush()
w("NICK owner"); w("USER owner 0 * :o"); time.sleep(0.8)
w("PRIVMSG NickServ :REGISTER pw12345 *"); time.sleep(0.8); w("QUIT"); time.sleep(0.2)
EOF
run ergo "127.0.0.1/$ERGO_PORT" owner pw12345 6 0 \
	"/bouncer" \
	"/bouncer on" \
	"/bouncer"
has 'can keep you online when nexchat is closed' "ergo: offered when off"
has 'The network will keep you online now' "ergo: turned on"
has 'You stay online on this network' "ergo: says it's on"

# --- a plain server (no bouncer, no always-on): explained
python3 "$here/fake_ircd.py" --log "$tmp/fake.log" --portfile "$tmp/fakeport" --timeout 30 &
fake=$!
i=0; while [ ! -s "$tmp/fakeport" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
run plain "127.0.0.1/$(cat "$tmp/fakeport")" ayush "" 0 0 "/bouncer"
kill $fake 2>/dev/null || true
has 'This is a plain IRC server' "plain: explained"
grep -q 'Connected through' "$tmp/out" && bad "plain: taken for a bouncer" || ok "plain: not taken for a bouncer"

if [ $fail -ne 0 ]; then
	
	echo "--- last"; cat "$tmp/out"
	exit 1
fi
echo "bouncers: ok"
exit 0

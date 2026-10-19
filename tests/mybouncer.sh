#!/bin/sh
# /bouncer setup, against the real soju and Ergo (tests/bnc): nexchat sets
# up its own soju (in the config dir, loopback only), moves a network onto
# it with its SASL login and channels, and connecting to that network
# then goes through soju and stays in the channel after nexchat quits.
# /bouncer remove puts the entry back. systemd isn't touched
# (NEXCHAT_BOUNCER_NO_SYSTEMD): here nexchat runs soju itself.
# usage: tests/mybouncer.sh path/to/nexchat-text   (77 = bouncers not fetched)
set -eu
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
. "$here/bnc/lab.sh"
bnc_have || { echo "mybouncer: not fetched (tests/bnc/fetch.sh), skipped"; exit 77; }
tmp=$(mktemp -d)
trap 'st=$?; [ -f "$tmp/cfg/bouncer/port" ] && pkill -f "soju -config $tmp/cfg/bouncer/config" 2>/dev/null; bnc_stop; rm -rf "$tmp"; exit $st' EXIT
bnc_start "$tmp/lab"
export NEXCHAT_SOJU_DIR=$SOJU_BIN_DIR NEXCHAT_BOUNCER_NO_SYSTEMD=1

fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }
has() { grep -q -- "$1" "$tmp/out" && ok "$2" || bad "$2"; }

# an account on the network, to log in with SASL
cat > "$tmp/reg.py" <<'EOF'
import socket, sys
s = socket.create_connection(("127.0.0.1", int(sys.argv[1])))
def w(l): s.sendall((l + "\r\n").encode())
w("NICK me1"); w("USER me1 0 * :x"); s.settimeout(20); b = b""
while b" 001 " not in b: b += s.recv(4096)
w("PRIVMSG NickServ :REGISTER pw12345 *")
while b"Account created" not in b: b += s.recv(4096)
w("QUIT")
EOF
python3 "$tmp/reg.py" "$ERGO_PORT"

mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = me1\n' > "$tmp/cfg/hexchat.conf"
# a network as people have it: SASL login, a channel, a favourite
printf 'v=2.16.2\n\nN=Lab\nI=me1\nU=me1\nP=pw12345\nL=6\nF=64\nD=0\nS=127.0.0.1/%s\nJ=#home\n\n' "$ERGO_PORT" > "$tmp/cfg/servlist.conf"

run () { # lines...
	{
		sleep 1
		for l in "$@"; do echo "$l"; sleep 1.5; done
		echo "/quit"; sleep 0.5
	} | timeout 40 "$bin" -n -d "$tmp/cfg" 2>&1 | sed 's/\x1b\[[0-9;]*m//g' > "$tmp/out" || true
}

run "/bouncer setup" "/echo waited"
has 'Your bouncer is running while nexchat is' "set up, and says how it's kept running"
has 'Lab: done' "the network was moved"
has '1 network moved to your bouncer' "and counted"
[ -f "$tmp/cfg/bouncer/config" ] && grep -q '127.0.0.1' "$tmp/cfg/bouncer/config" && ok "its config is in nexchat's folder, loopback only" || bad "config"
perm=$(stat -c %a "$tmp/cfg/bouncer/password" 2>/dev/null || stat -f %Lp "$tmp/cfg/bouncer/password")
[ "$perm" = 600 ] && ok "its password file is private ($perm)" || bad "password file mode $perm"
port=$(cat "$tmp/cfg/bouncer/port")
grep -A12 '^N=Lab$' "$tmp/cfg/servlist.conf" | grep -q "^S=127.0.0.1/$port$" && ok "the entry now goes through the bouncer" || { bad "entry"; cat "$tmp/cfg/servlist.conf"; }
grep -A12 '^N=Lab$' "$tmp/cfg/servlist.conf" | grep -q '^U=me/Lab$' && ok "and logs in to it as me/Lab" || bad "entry user"
grep -q 'pw12345' "$tmp/cfg/bouncer/soju.log" && bad "the network password ended up in soju's log" || ok "no password in soju's log"

# connect: through soju, logged in to the network with SASL, in #home
run "/server Lab" "/echo waited" "/echo waited"
pgrep -f "soju -config $tmp/cfg/bouncer/config" > /dev/null && ok "soju keeps running after nexchat quits" || bad "soju quit with nexchat"
has 'BOUNCER_NETID=' "connecting to Lab goes through soju"
has 'Now talking on #home' "and it's in the channel that was a favourite"
# from outside, with nexchat closed: is soju on the network for us?
# (it reconnects on its own schedule: asked again for up to 10 s)
python3 - "$ERGO_PORT" > "$tmp/whois" <<'EOF'
import socket, sys, time
s = socket.create_connection(("127.0.0.1", int(sys.argv[1])))
def w(l): s.sendall((l + "\r\n").encode())
w("NICK checker"); w("USER c 0 * :c"); s.settimeout(10); b = b""
while b" 001 " not in b: b += s.recv(4096)
# Ergo shows invisible users (its default) only to channel members
w("JOIN #home")
for _ in range(10):
    w("WHOIS me1"); w("NAMES #home")
    t = b""
    while b" 366 " not in t: t += s.recv(4096)
    names = [l for l in t.split(b"\r\n") if b" 353 " in l]
    if b" 330 " in t and any(b"me1" in l for l in names):
        break
    time.sleep(1)
print(t.decode())
EOF
grep -q ' 330 checker me1 me1' "$tmp/whois" && ok "soju logged in to the network as the account (SASL)" || { bad "not logged in"; grep " 3[0-9][0-9] " "$tmp/whois"; }
grep ' 353 ' "$tmp/whois" | grep -q 'me1' && ok "still in #home with nexchat closed" || { bad "not in #home after quitting"; cat "$tmp/whois"; }

# change its settings: each lands in soju
run "/bouncer set net Lab realname Real Name Here" "/bouncer set channel Lab #other add" \
	"/bouncer set channel Lab #other detached on" "/bouncer set commands Lab PRIVMSG me1 :hi|PRIVMSG me1 :there" \
	"/bouncer set title My Bouncer" "/echo waited" "/bouncer set logs files" "/echo waited" "/bouncer set"
[ "$(grep -c 'Done\.' "$tmp/out")" = 6 ] && ok "every setting was taken" || { bad "a setting failed"; grep -i '\*' "$tmp/out"; }
grep -q '^title "My Bouncer"' "$tmp/cfg/bouncer/config" && ok "title in soju's config" || bad "title"
grep -q '^message-store fs ' "$tmp/cfg/bouncer/config" && ok "logs kept as files" || bad "logs as files"
has 'logs files, title "My Bouncer"' "/bouncer set shows them"
pgrep -f "soju -config $tmp/cfg/bouncer/config" > /dev/null && ok "soju running again after the restart" || bad "soju didn't come back"
"$SOJU_BIN_DIR/sojuctl" -config "$tmp/cfg/bouncer/config" user run me network status > "$tmp/ns" 2>&1 || true
"$SOJU_BIN_DIR/sojuctl" -config "$tmp/cfg/bouncer/config" user run me channel status -network Lab > "$tmp/cs" 2>&1 || true
grep -q '#other' "$tmp/cs" && grep '#other' "$tmp/cs" | grep -q detached && ok "#other added, detached" || { bad "channel"; cat "$tmp/cs"; }

# put it back
run "/bouncer remove"
has "connect directly again" "remove says what happened"
grep -A12 "^N=Lab$" "$tmp/cfg/servlist.conf" | grep -q "^S=127.0.0.1/$ERGO_PORT$" && ok "the entry has its own address back" || bad "not restored"
grep -A12 "^N=Lab$" "$tmp/cfg/servlist.conf" | grep -q "^P=pw12345$" && grep -A12 "^N=Lab$" "$tmp/cfg/servlist.conf" | grep -q "^L=6$" && ok "and its own password and SASL login" || bad "login not restored"

pgrep -f "soju -config $tmp/cfg/bouncer/config" > /dev/null && bad "soju still running after remove" || ok "soju stopped by remove"

# set up again: soju's database still has the account
run "/bouncer setup" "/echo waited" "/bouncer remove"
has 'Lab: done' "set up again after remove"
grep -q "Couldn't make an account" "$tmp/out" && bad "account already there" || ok "the old account is reused"

[ $fail -eq 0 ] || { cat "$tmp/out"; exit 1; }
echo "mybouncer: ok"
exit 0

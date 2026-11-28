#!/bin/sh
# soju, the real one (tests/bnc/lab.sh): detected; /bouncer lists, adds and
# removes its networks; they show up as Network List entries that connect
# straight onto that network.
# usage: tests/bouncers-soju.sh path/to/nexchat-text   (77 = bouncers not fetched)
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
	# each line goes once the client is in, the next once the reply has come
	# (output quiet for a moment): no fixed pauses
	for l in "$@"; do printf '%s\n' "$l"; done |
		timeout 60 python3 "$here/textdrive.py" "$tmp/out" -- "$bin" -n -d "$tmp/cfg" "irc://$name" || true
}

# F=0: no global user info, no TLS. L=7 server password (PASS), L=6 SASL
# --- soju: detected, networks listed, add and remove one
run soju "127.0.0.1/$SOJU_PORT" ayush secret123 7 0 \
	"/bouncer" \
	"/bouncer add ergonet 127.0.0.1:$ERGO_PORT -notls" \
	"/bouncer" \
	"/bouncer del ergonet" \
	"/bouncer"
has 'Connected through soju' "soju: detected"
has '(none yet' "soju: no networks at first"
has 'ergonet added' "soju: add said so"
has "ergonet  127.0.0.1:$ERGO_PORT" "soju: the new network listed with its address"
has 'Removed from the bouncer' "soju: remove said so"
n=$(grep -c "ergonet  127.0.0.1" "$tmp/out" || true)
[ "$n" -eq 1 ] && ok "soju: gone after removing (listed once)" || bad "soju: listed $n times"
cp "$tmp/out" "$tmp/soju.out"

# --- soju: its networks become Network List entries, and one connects
# straight onto its network
keepcfg=1 run soju "127.0.0.1/$SOJU_PORT" ayush secret123 7 0 \
	"/bouncer add libera 127.0.0.1:$ERGO_PORT -notls"
grep -q '^N=soju/libera$' "$tmp/cfg/servlist.conf" && ok "soju: its network is an entry in the Network List" ||
	{ bad "soju: no entry"; cat "$tmp/cfg/servlist.conf"; }
grep -A3 '^N=soju/libera$' "$tmp/cfg/servlist.conf" | grep -q '^U=ayush/libera$' &&
	ok "soju: the entry logs in as ayush/libera" || bad "soju: entry user"
printf '%s\n' "/server soju/libera" "?NETWORK=ErgoTest" |
	timeout 30 python3 "$here/textdrive.py" "$tmp/out" --offline -- "$bin" -n -d "$tmp/cfg" -a || true
grep -q 'BOUNCER_NETID=' "$tmp/out" && grep -q 'NETWORK=ErgoTest' "$tmp/out" &&
	ok "soju: connecting with the entry lands on that network" || { bad "soju: entry connect"; tail -20 "$tmp/out"; }
keepcfg=1 run soju "127.0.0.1/$SOJU_PORT" ayush secret123 7 0 "/bouncer del libera"
grep -q '^N=soju/libera$' "$tmp/cfg/servlist.conf" && bad "soju: entry stays after removing" ||
	ok "soju: removed on the bouncer, gone from the list"

# --- /bouncer remove NAME doesn't remove anything: that's /bouncer del
keepcfg=1 run soju "127.0.0.1/$SOJU_PORT" ayush secret123 7 0 \
	"/bouncer add keepnet 127.0.0.1:$ERGO_PORT -notls" \
	"/bouncer remove keepnet" \
	"/bouncer"
has '/bouncer del keepnet' "soju: remove with a name points to del"
has "keepnet  127.0.0.1:$ERGO_PORT" "soju: and the network is still there"
keepcfg=1 run soju "127.0.0.1/$SOJU_PORT" ayush secret123 7 0 "/bouncer del keepnet"

# --- a network going onto the bouncer: the entry that connected to it
# directly at startup stops doing so (not on the network twice)
printf 'N=direct\nU=ayush\nF=8\nD=0\nS=127.0.0.1/%s\n\n' "$ERGO_PORT" >> "$tmp/cfg/servlist.conf"
keepcfg=1 run soju "127.0.0.1/$SOJU_PORT" ayush secret123 7 0 \
	"/bouncer add twin 127.0.0.1:$ERGO_PORT -notls"
has 'direct is on your bouncer now, as soju/twin' "soju: the direct entry's startup connect is said to stop"
grep -A3 '^N=direct$' "$tmp/cfg/servlist.conf" | grep -q '^F=0$' &&
	ok "soju: and it doesn't auto-connect any more" || { bad "soju: direct entry flags"; grep -A3 '^N=direct$' "$tmp/cfg/servlist.conf"; }
keepcfg=1 run soju "127.0.0.1/$SOJU_PORT" ayush secret123 7 0 "/bouncer del twin"

# --- connecting to soju itself opens each of its networks in a tab
keepcfg=1 run soju "127.0.0.1/$SOJU_PORT" ayush secret123 7 0 \
	"/bouncer add tabnet 127.0.0.1:$ERGO_PORT -notls"
printf '%s\n' "?opened in (a )?tab" "?BOUNCER_NETID=" |
	timeout 30 python3 "$here/textdrive.py" "$tmp/out" -- "$bin" -n -d "$tmp/cfg" "irc://soju" || true
has 'opened in \(a \)\?tab' "soju: its networks open by themselves"
grep -q 'BOUNCER_NETID=' "$tmp/out" && ok "soju: and connect straight onto them" || bad "soju: no network tab"

# --- the network behind soju goes down while we're on it: said plainly
# (soju sends the state change because we asked for -notify)
keepcfg=1 run soju "127.0.0.1/$SOJU_PORT" ayush secret123 7 0 \
	"/bouncer add downnet 127.0.0.1:$ERGO_PORT -notls"
printf '%s\n' "/server soju/downnet" "?BOUNCER_NETID=" "!kill $(echo $BNC_PIDS | cut -d' ' -f1) 2>/dev/null" "?soju lost downnet" |
	timeout 40 python3 "$here/textdrive.py" "$tmp/out" --offline -- "$bin" -n -d "$tmp/cfg" -a || true
has 'soju lost downnet' "soju: losing the network behind it is said"

if [ $fail -ne 0 ]; then
	
	echo "--- last"; cat "$tmp/out"
	exit 1
fi
echo "bouncers-soju: ok"
exit 0

#!/bin/sh
# /lastlog -s against the real soju (tests/bnc/lab.sh): searches the
# bouncer's history, including what was said while nexchat was closed.
# usage: tests/bouncers-search.sh path/to/nexchat-text   (77 = bouncers not fetched)
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
# --- /lastlog -s: search soju's history, also what was said while
# nexchat was closed
keepcfg=1 run soju "127.0.0.1/$SOJU_PORT" ayush secret123 7 0 \
	"/bouncer add srch 127.0.0.1:$ERGO_PORT -notls"
# join once (soju stays in #srch for us), then go away
{
	sleep 1; echo "/server soju/srch"; sleep 3; echo "/join #srch"; sleep 2; echo "/quit"; sleep 0.5
} | timeout 20 "$bin" -n -d "$tmp/cfg" -a >/dev/null 2>&1 || true
bnc_say bob "#srch" "the Release is out" "unrelated chatter" "release notes are up"
sleep 1
{
	sleep 1; echo "/server soju/srch"; sleep 4
	echo "/lastlog -s release"; sleep 2
	echo "/lastlog -s zzznothing"; sleep 2
	echo "/quit"; sleep 0.5
} | timeout 25 "$bin" -n -d "$tmp/cfg" -a 2>&1 | sed 's/\x1b\[[0-9;]*m//g' > "$tmp/out" || true
has "Searching the bouncer's history of 1 place" "search: asks soju"
has '<bob> the Release is out' "search: finds what was said while closed"
has '<bob> release notes are up' "search: every match"
grep -q 'unrelated chatter' "$tmp/out" && grep 'unrelated chatter' "$tmp/out" | grep -qv 'bob>' && true
n=$(grep -c '  <bob> .*elease' "$tmp/out" || true)
[ "$n" -eq 2 ] && ok "search: no extra lines ($n matches)" || bad "search: $n result lines"
has '2 matches on the bouncer' "search: counted"
has 'No matches for "zzznothing" on the bouncer' "search: nothing found is said"

if [ $fail -ne 0 ]; then
	
	echo "--- last"; cat "$tmp/out"
	exit 1
fi
echo "bouncers-search: ok"
exit 0

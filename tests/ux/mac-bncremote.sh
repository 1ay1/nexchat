#!/bin/sh
# tests/ux/bncremote.sh without Xvfb: the same Bouncer window checks on the
# native macOS backend (Homebrew's GTK has no X11). The window lists a soju
# elsewhere, then nick, real name and network login are changed from it and
# land in soju.
# usage: tests/ux/mac-bncremote.sh path/to/nexchat
set -eu
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../bnc/lab.sh"
bnc_have || { echo "mac-bncremote: bouncers not fetched (tests/bnc/fetch.sh), skipped"; exit 77; }
tmp=$(mktemp -d)
app=
cleanup() { [ -n "$app" ] && kill $app 2>/dev/null; bnc_stop; rm -rf "$tmp"; }
trap 'st=$?; cleanup; exit $st' EXIT
bnc_start "$tmp/lab"
export NEXCHAT_SOJU_DIR=$SOJU_BIN_DIR NEXCHAT_BOUNCER_NO_SYSTEMD=1 GTK_A11Y=none

fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }
# the newest "bncwin:" line the app printed
state() { grep '^bncwin:' "$tmp/log" | tail -1; }
until_state() { i=0; while [ $i -lt ${2:-40} ]; do case "$(state)" in $1) return 0;; esac; i=$((i+1)); sleep 0.5; done; return 1; }

# an account on Ergo for the SASL login, and a network on soju
python3 - "$ERGO_PORT" <<'EOF'
import socket, sys, time
s = socket.create_connection(("127.0.0.1", int(sys.argv[1])))
for l in ["NICK acct2", "USER a 0 * :a", "PRIVMSG NickServ :REGISTER pw2pw2pw2"]:
    s.sendall((l + "\r\n").encode()); time.sleep(0.5)
time.sleep(1); s.close()
EOF
python3 - "$SOJU_PORT" "$ERGO_PORT" <<'EOF'
import socket, sys, time
s = socket.create_connection(("127.0.0.1", int(sys.argv[1])))
for l in ["PASS secret123", "NICK x", "USER ayush 0 * :x",
          "PRIVMSG BouncerServ :network create -addr irc+insecure://127.0.0.1:%s -name ergo -nick dee" % sys.argv[2]]:
    s.sendall((l + "\r\n").encode()); time.sleep(0.5)
time.sleep(1); s.close()
EOF

mkdir -p "$tmp/cfg"
printf 'gui_slist_skip = 1\nirc_nick1 = dee\nnet_throttle = 0\n' > "$tmp/cfg/hexchat.conf"
printf 'v=2.16.2\n\nN=Elsewhere\nI=ayush\nU=ayush\nP=secret123\nL=7\nF=8\nD=0\nS=127.0.0.1/%s\n\n' "$SOJU_PORT" > "$tmp/cfg/servlist.conf"
NEXCHAT_TABS_DUMP=1 NEXCHAT_BNC_WIN=1 NEXCHAT_BNCWIN_REDIT="$tmp/redit" "$bin" -n -d "$tmp/cfg" >"$tmp/log" 2>&1 &
app=$!

until_state "bncwin: Your bouncer*remote *: connected as *" && ok "lists the bouncer and its network" ||
	{ bad "not listed"; state; }
grep -q '^N=Elsewhere$' "$tmp/cfg/servlist.conf" && grep -A8 '^N=Elsewhere$' "$tmp/cfg/servlist.conf" | grep -q '^F=.*' &&
	ok "remembered as a bouncer" || bad "entry"
net=$(state | sed -n 's/.*| remote \([^:]*\): .*/\1/p')
printf '%s\tdeenew\tDee Real\tacct2\tpw2pw2pw2\n' "$net" > "$tmp/redit"
until_state "*remote $net: connected as deenew*" 60 && ok "new nick saved on soju" || { bad "nick"; state; }

# (the state above is soju's own BOUNCER NETWORK notification: the nick
# shown is the one soju has now)

[ $fail -eq 0 ] && echo "mac-bncremote: ok"
exit $fail

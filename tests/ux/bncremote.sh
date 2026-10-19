#!/bin/sh
# Server > Bouncer while connected to a soju elsewhere (not one nexchat
# runs): its networks are listed, and nick, real name and the network
# login are changed from the window and land in soju.
# usage: tests/ux/bncremote.sh path/to/nexchat
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../bnc/lab.sh"
bnc_have || { echo "bncremote: bouncers not fetched (tests/bnc/fetch.sh), skipped"; exit 77; }
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:78}
tmp=$(mktemp -d)
app=; xv=
cleanup() { [ -n "$app" ] && kill $app 2>/dev/null; bnc_stop; kill $xv 2>/dev/null; rm -rf "$tmp"; }
trap 'st=$?; cleanup; exit $st' EXIT
start_xvfb "$xvfb" "$disp"
bnc_start "$tmp/lab"
# soju has one network, to Ergo
export NEXCHAT_SOJU_DIR=$BNC_HOME/usr/bin NEXCHAT_BOUNCER_NO_SYSTEMD=1

fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }
state() {
	n0=$(grep -c '^tabs:$' "$tmp/log"); i=0
	while [ "$(grep -c '^tabs:$' "$tmp/log")" -lt $((n0 + 2)) ]; do
		i=$((i+1)); [ $i -gt 100 ] && { echo "(no dump)"; return; }; sleep 0.1
	done
	awk '/^tabs:$/{n++} {r[n]=r[n] $0 "\n"} END{printf "%s", r[n-1]}' "$tmp/log" | grep "^$1:" | head -1
}
until_state() { i=0; while [ $i -lt ${3:-40} ]; do case "$(state "$1")" in $2) return 0;; esac; i=$((i+1)); sleep 0.5; done; return 1; }

# an account on Ergo for the SASL login
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
DISPLAY=$disp NEXCHAT_TABS_DUMP=1 NEXCHAT_BNC_WIN=1 NEXCHAT_BNCWIN_REDIT="$tmp/redit" "$bin" -d "$tmp/cfg" >"$tmp/log" 2>&1 &
app=$!

until_state bncwin "bncwin: You're using a bouncer*remote *: connected as *" && ok "lists the bouncer's network" ||
	{ bad "not listed"; state bncwin; }
net=$(state bncwin | sed -n 's/.*| remote \([^:]*\): .*/\1/p')
printf '%s\tdeenew\tDee Real\tacct2\tpw2pw2pw2\n' "$net" > "$tmp/redit"
until_state bncwin "*remote $net: connected as deenew*" 60 && ok "new nick saved on soju" || { bad "nick"; state bncwin; }

# what soju has now
python3 - "$SOJU_PORT" "$net" > "$tmp/soju" <<'EOF'
import socket, sys, time
s = socket.create_connection(("127.0.0.1", int(sys.argv[1]))); s.settimeout(0.5)
for l in ["CAP LS 302", "CAP REQ :soju.im/bouncer-networks", "CAP END", "PASS secret123", "NICK x", "USER ayush 0 * :x"]:
    s.sendall((l + "\r\n").encode())
time.sleep(1.5)
s.sendall(b"BOUNCER LISTNETWORKS\r\n")
s.sendall(("PRIVMSG BouncerServ :sasl status -network " + sys.argv[2] + "\r\n").encode())
b = b""; t = time.time()
while time.time() - t < 3:
    try: b += s.recv(4096)
    except Exception: pass
print(b.decode(errors="replace"))
EOF
grep -q 'realname=Dee\\sReal' "$tmp/soju" && ok "real name on soju" || { bad "realname"; grep BOUNCER "$tmp/soju"; }
grep -q 'SASL PLAIN enabled with username "acct2"' "$tmp/soju" && ok "SASL login set on soju" || { bad "sasl"; grep BouncerServ "$tmp/soju"; }
grep -q 'Authenticated on upstream network with account "acct2"' "$tmp/soju" && ok "and soju logged in with it" ||
	{ bad "not logged in"; grep BouncerServ "$tmp/soju"; }

[ $fail -eq 0 ] || { tail -30 "$tmp/log"; exit 1; }
echo "bncremote: ok"

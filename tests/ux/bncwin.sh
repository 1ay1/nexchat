#!/bin/sh
# Server > Bouncer, the one place for the bouncer nexchat runs: soju found
# installed, one button sets it up and the network it was opened from goes
# on it (nothing to pick), each network's live state from soju, switch one
# off and it's back to connecting directly, stop/start, remove.
# usage: tests/ux/bncwin.sh path/to/nexchat   (Xvfb + XTest + openbox)
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../bnc/lab.sh"
bnc_have || { echo "bncwin: bouncers not fetched (tests/bnc/fetch.sh), skipped"; exit 77; }
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:77}
tmp=$(mktemp -d)
app=; xv=; wm=
cleanup() {
	set +e
	[ -n "$app" ] && kill $app 2>/dev/null
	[ -n "$wm" ] && kill $wm 2>/dev/null
	pkill -f "soju -config $tmp/cfg/bouncer/config" 2>/dev/null
	[ -n "${KEEP:-}" ] && cp "$tmp/log" "$KEEP" 2>/dev/null
	bnc_stop; kill $xv 2>/dev/null; rm -rf "$tmp"
	return 0
}
trap 'st=$?; cleanup; exit $st' EXIT
start_xvfb "$xvfb" "$disp"
ob=${OPENBOX:-$(command -v openbox || true)}
obref=$HOME/.cache/nexchat-ref/ob/usr
[ -z "$ob" ] && [ -x "$obref/bin/openbox" ] && ob=$obref/bin/openbox
[ -n "$ob" ] || { echo "bncwin: no openbox, skipped"; exit 77; }
DISPLAY=$disp LD_LIBRARY_PATH=$obref/lib XDG_CONFIG_DIRS=$obref/../etc/xdg XDG_DATA_DIRS=$obref/share "$ob" --sm-disable >/dev/null 2>&1 &
wm=$!
bnc_start "$tmp/lab"
export NEXCHAT_SOJU_DIR=$SOJU_BIN_DIR NEXCHAT_BOUNCER_NO_SYSTEMD=1

k() { DISPLAY=$disp python3 "$here/xkey.py" --delay 0.15 "$@"; }
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
press_until() { n=0; while [ $n -lt 5 ]; do k $1; until_state "$2" "$3" 6 && return 0; n=$((n+1)); done; return 1; }

mkdir -p "$tmp/cfg"
printf 'gui_slist_skip = 1\nirc_nick1 = dee\nnet_throttle = 0\n' > "$tmp/cfg/hexchat.conf"
printf 'v=2.16.2\n\nN=Lab\nF=2\nD=0\nS=127.0.0.1/%s\nJ=#den\n\n' "$ERGO_PORT" > "$tmp/cfg/servlist.conf"
# opened from Lab (as Server > Bouncer on Lab's tab would)
DISPLAY=$disp NEXCHAT_TABS_DUMP=1 NEXCHAT_BNC_WIN=Lab NEXCHAT_BNCWIN_FLIP="$tmp/flip" "$bin" -a -d "$tmp/cfg" >"$tmp/log" 2>&1 &
app=$!

until_state bncwin 'bncwin: soju is installed*' && ok "says soju is installed, not set up yet" || { bad "head"; state bncwin; }
press_until alt+s bncwin 'bncwin: Your bouncer is running*' && ok "one button: running" || { bad "setup"; state bncwin; }
until_state bncwin '*Lab \[on\] *in 1 channels*' && ok "Lab went on it by itself, and soju reports it in its channel" ||
	{ bad "Lab not on it"; state bncwin; }
until_state title '*online 24/7*' && ok "Lab connected through it" || { bad "title"; state title; }
until_state bncwin '*Lab \[on\]*as dee (your nick in the Network List), no login*' &&
	ok "shows the nick it uses, where it comes from, and the login" || { bad "who"; state bncwin; }

# change the nick in the settings, connect again: soju is told, keeps it
kill $app; wait $app 2>/dev/null || true
sed -i 's/^irc_nick1 = dee$/irc_nick1 = deenew/' "$tmp/cfg/hexchat.conf"
DISPLAY=$disp NEXCHAT_TABS_DUMP=1 "$bin" -a -d "$tmp/cfg" -c "server Lab" >"$tmp/log" 2>&1 &
app=$!
i=0; while [ $i -lt 30 ]; do grep -q "you are now known as deenew\|now known as deenew" "$tmp/log" && break; sleep 0.5; i=$((i+1)); done
cat > "$tmp/who.py" <<'EOF'
import socket, sys, time
s = socket.create_connection(("127.0.0.1", int(sys.argv[1])))
def w(l): s.sendall((l + "\r\n").encode())
w("NICK chk2"); w("USER c 0 * :c"); s.settimeout(10); b = b""
while b" 001 " not in b: b += s.recv(4096)
w("JOIN #den")
for _ in range(15):
    w("NAMES #den"); t = b""
    while b" 366 " not in t: t += s.recv(4096)
    if b"deenew" in t: break
    time.sleep(1)
print(t.decode())
EOF
python3 "$tmp/who.py" "$ERGO_PORT" | grep -q 'deenew' && ok "a new nick in the settings is used on the network" || bad "old nick kept"
kill $app; wait $app 2>/dev/null || true
DISPLAY=$disp NEXCHAT_TABS_DUMP=1 NEXCHAT_BNC_WIN=Lab NEXCHAT_BNCWIN_FLIP="$tmp/flip" NEXCHAT_BNCWIN_EDIT="$tmp/edit" NEXCHAT_BNCWIN_TAB="$tmp/tab" NEXCHAT_SHOT_WIN=1 NEXCHAT_SHOT_ON="$tmp/shot" "$bin" -a -d "$tmp/cfg" >"$tmp/log" 2>&1 &
app=$!
until_state bncwin '*Lab \[on\]*as deenew*' && ok "the window shows the new nick" || { bad "window nick"; state bncwin; }

# Nick and login...: its own nick, and an account to log in with
cat > "$tmp/reg.py" <<'EOF'
import socket, sys
s = socket.create_connection(("127.0.0.1", int(sys.argv[1])))
def w(l): s.sendall((l + "\r\n").encode())
w("NICK acct1"); w("USER a 0 * :a"); s.settimeout(20); b = b""
while b" 001 " not in b: b += s.recv(4096)
w("PRIVMSG NickServ :REGISTER pw12345 *")
while b"Account created" not in b: b += s.recv(4096)
w("QUIT")
EOF
python3 "$tmp/reg.py" "$ERGO_PORT"
echo "Lab	acct1	acct1	pw12345" > "$tmp/edit"
until_state bncwin '*Lab \[on\]*as acct1 (this network*logged in to the account acct1*' &&
	ok "nick and login changed from the window" || { bad "edit"; state bncwin; }
cat > "$tmp/who.py" <<'EOF'
import socket, sys, time
s = socket.create_connection(("127.0.0.1", int(sys.argv[1])))
def w(l): s.sendall((l + "\r\n").encode())
w("NICK chk3"); w("USER c 0 * :c"); s.settimeout(10); b = b""
while b" 001 " not in b: b += s.recv(4096)
for _ in range(20):
    w("WHOIS acct1"); t = b""
    while b" 318 " not in t: t += s.recv(4096)
    if b" 330 " in t: break
    time.sleep(1)
print(t.decode())
EOF
python3 "$tmp/who.py" "$ERGO_PORT" | grep -q ' 330 chk3 acct1 acct1' && ok "soju is on the network as acct1, logged in" || bad "not logged in as acct1"

# switched off: back to its own address
echo Lab > "$tmp/flip"
until_state bncwin '*Lab \[off\] connects directly*' && ok "switched off: connects directly" || { bad "off"; state bncwin; }
grep -A10 '^N=Lab$' "$tmp/cfg/servlist.conf" | grep -q "^S=127.0.0.1/$ERGO_PORT$" && ok "its own address back" || bad "address"
# and on again
echo Lab > "$tmp/flip"
until_state bncwin '*Lab \[on\] *in 1 channels*' && ok "on again" || { bad "on again"; state bncwin; }

# the other tabs
shot() { [ -n "${SHOTS:-}" ] || return 0; sleep 1; echo "$SHOTS/$1.png" > "$tmp/shot"
	n=0; while [ -e "$tmp/shot" ] && [ $n -lt 20 ]; do sleep 0.3; n=$((n+1)); done; }
shot networks
echo 1 > "$tmp/tab"
until_state bncwin '*channels: #den joined*' && ok "Channels: the channel it's in" || { bad "channels"; state bncwin; }
shot channels
echo 2 > "$tmp/tab"
until_state bncwin '*access: On this computer: 127.0.0.1 port *User name: me/network*remote off*' &&
	ok "Access: where and who to log in as" || { bad "access"; state bncwin; }
shot access
echo 3 > "$tmp/tab"
until_state bncwin '*storage: db, title ""*' && ok "Storage: history in the database" || { bad "storage"; state bncwin; }
shot storage
echo 0 > "$tmp/tab"
until_state bncwin '*Lab \[on\]*' && ok "back to Networks" || bad "networks tab"

# stop and start
press_until alt+t bncwin 'bncwin: Your bouncer is stopped*' && ok "stop" || bad "stop"
until_state bncwin '*Lab \[on\] *bouncer stopped*' && ok "the network says the bouncer is stopped" || { bad "stopped state"; state bncwin; }
press_until alt+s bncwin 'bncwin: Your bouncer is running*' && ok "start" || bad "start"
until_state bncwin '*Lab \[on\] *in 1 channels*' && ok "back in its channel" || { bad "after start"; state bncwin; }

[ $fail -eq 0 ] || exit 1
echo "bncwin: ok"
exit 0

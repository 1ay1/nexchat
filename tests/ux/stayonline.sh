#!/bin/sh
# "Stay online" against real soju, ZNC and Ergo (tests/bnc/lab.sh):
#   choose -> bouncer (checked as you type) -> which networks -> Connect,
# and the networks ticked end up on the bouncer and in the Network List.
# Also: a wrong password, a self-signed certificate, a plain server, the
# network's own always-on, and the explanation.
# usage: tests/ux/stayonline.sh path/to/nexchat   (Xvfb + XTest + openbox)
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../bnc/lab.sh"
bnc_have || { echo "stayonline: bouncers not fetched (tests/bnc/fetch.sh), skipped"; exit 77; }
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:85}
tmp=$(mktemp -d)
app=; xv=; wm=
trap 'st=$?; [ -n "$app" ] && kill $app 2>/dev/null; [ -n "$wm" ] && kill $wm 2>/dev/null; bnc_stop; kill $xv 2>/dev/null; rm -rf "$tmp"; exit $st' EXIT
start_xvfb "$xvfb" "$disp"
ob=${OPENBOX:-$(command -v openbox || true)}
obref=$HOME/.cache/nexchat-ref/ob/usr
[ -z "$ob" ] && [ -x "$obref/bin/openbox" ] && ob=$obref/bin/openbox
[ -n "$ob" ] || { echo "stayonline: no openbox, skipped"; exit 77; }
DISPLAY=$disp LD_LIBRARY_PATH=$obref/lib XDG_CONFIG_DIRS=$obref/../etc/xdg XDG_DATA_DIRS=$obref/share "$ob" --sm-disable >/dev/null 2>&1 &
wm=$!
bnc_start "$tmp/lab"

k() { DISPLAY=$disp python3 "$here/xkey.py" --delay 0.15 "$@"; }
t() { DISPLAY=$disp python3 "$here/xkey.py" --delay 0.02 --type "$1"; }

fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }

# the last full dump's line for KEY
state() {
	n0=$(grep -c '^tabs:$' "$tmp/log")
	i=0
	while [ "$(grep -c '^tabs:$' "$tmp/log")" -lt $((n0 + 2)) ]; do
		i=$((i+1)); [ $i -gt 100 ] && { echo "(no dump)"; return; }
		sleep 0.1
	done
	awk '/^tabs:$/{n++} {r[n]=r[n] $0 "\n"} END{printf "%s", r[n-1]}' "$tmp/log" | grep "^$1:" | head -1
}
until_state() { # KEY PATTERN, up to 15 s
	i=0
	while [ $i -lt 30 ]; do
		case "$(state "$1")" in $2) return 0;; esac
		i=$((i+1)); sleep 0.5
	done
	return 1
}

start () {
	if [ -n "$app" ]; then kill $app 2>/dev/null || true; wait $app 2>/dev/null || true; fi
	rm -rf "$tmp/cfg"; mkdir -p "$tmp/cfg"
	printf 'gui_slist_skip = 1\nirc_nick1 = ayush\nnet_throttle = 0\ngui_win_left = 0\ngui_win_top = 0\n' > "$tmp/cfg/hexchat.conf"
	DISPLAY=$disp NEXCHAT_TABS_DUMP=1 NEXCHAT_BNC_SETUP=1 "$bin" -a -d "$tmp/cfg" >"$tmp/log" 2>&1 &
	app=$!
	until_state bncsetup 'bncsetup: choose*' || { bad "window didn't open"; return 1; }
}

# fill the bouncer page: address, user, password (checked by itself).
# The first key can land before the window manager has focused the new
# window, so it's tried again a few times.
choose () { # KEY PAGE-PATTERN
	tries=0
	while :; do
		k $1
		until_state_short bncsetup "$2" && return 0
		tries=$((tries+1))
		[ $tries -ge 5 ] && { bad "didn't get to $2"; return 1; }
	done
}
bouncer_page () { # address user pass
	choose alt+h 'bncsetup: bouncer*' || return 1
	k alt+a; t "$1"; k alt+u; t "$2"; k alt+p; t "$3"
}
until_state_short() { # KEY PATTERN, up to 3 s
	i=0
	while [ $i -lt 6 ]; do
		case "$(state "$1")" in $2) return 0;; esac
		i=$((i+1)); sleep 0.5
	done
	return 1
}

# ---- soju, end to end: picks two networks, they end up on soju
start
bouncer_page "irc://127.0.0.1:$SOJU_PORT" ayush secret123
until_state bncsetup "*That's soju, and the password is right*next=yes" &&
	ok "soju: checked by itself while typing; Next allowed" || { bad "soju: check"; state bncsetup; }
k Return
until_state bncsetup 'bncsetup: networks*' && ok "soju: on to choosing networks" || bad "soju: networks page"
# Another one: the lab's Ergo, as two networks (plain text, its port)
k alt+o; t "127.0.0.1:$ERGO_PORT"
k alt+m; t "labnet"
k alt+n
until_state title "*online 24/7*" && ok "soju: connected, the title says online 24/7" || { bad "soju: title"; state title; }
# the queue: labnet added on soju, then synced into the Network List
i=0; while [ $i -lt 30 ] && ! grep -q '^N=My bouncer (127.0.0.1)/labnet$' "$tmp/cfg/servlist.conf" 2>/dev/null; do sleep 0.5; i=$((i+1)); done
grep -q '^N=My bouncer (127.0.0.1)/labnet$' "$tmp/cfg/servlist.conf" &&
	ok "soju: the network asked for was added on soju, and is in the Network List" ||
	{ bad "soju: labnet not added"; cat "$tmp/cfg/servlist.conf"; }
grep -q 'labnet' "$tmp/lab/soju/soju.log" && ok "soju: soju connected to it" || bad "soju: soju didn't try labnet"

# ---- soju: a wrong password
start
bouncer_page "irc://127.0.0.1:$SOJU_PORT" ayush nope
until_state bncsetup "*password is wrong*next=no" && ok "wrong password: said, and Next stays off" ||
	{ bad "wrong password"; state bncsetup; }

# ---- soju over TLS with a self-signed certificate
start
bouncer_page "127.0.0.1:$SOJU_TLS_PORT" ayush secret123
until_state bncsetup "*That's soju*self-signed*next=no" &&
	ok "tls: the certificate is pointed out, and Next waits" || { bad "tls: no warning"; state bncsetup; }
k alt+t
until_state bncsetup "*next=yes" && ok "tls: ticking 'trust' allows Next" || bad "tls: trust"
k Return; k alt+n
sleep 3
flags=$(awk -v s="S=127.0.0.1/+$SOJU_TLS_PORT" '/^N=/{if (hit) exit; f=""} /^F=/{f=$0} $0==s{hit=1} END{if (hit) print f}' "$tmp/cfg/servlist.conf" | cut -d= -f2)
[ -n "$flags" ] && [ $(( flags & 36 )) -eq 36 ] && ok "tls: saved with TLS and that certificate allowed" ||
	{ bad "tls: flags '$flags'"; cat "$tmp/cfg/servlist.conf"; }

# ---- ZNC
start
bouncer_page "irc://127.0.0.1:$ZNC_PORT" ayush secret123
until_state bncsetup "*That's ZNC, and the password is right*next=yes" && ok "znc: checked" || { bad "znc"; state bncsetup; }

# ---- a plain IRC server typed as the bouncer
start
bouncer_page "irc://127.0.0.1:$ERGO_PORT" x y
until_state bncsetup "*not a bouncer*keep you online*next=no" &&
	ok "plain: an IRC network given as the bouncer is caught, and pointed to the other choice" ||
	{ bad "plain"; state bncsetup; }

# ---- the network's own always-on (Ergo), from the Network List entry
python3 - "$ERGO_PORT" <<'EOF'
import socket, sys
s = socket.create_connection(("127.0.0.1", int(sys.argv[1])))
def w(l): s.sendall((l + "\r\n").encode())
w("NICK owner"); w("USER owner 0 * :o"); s.settimeout(20); b = b""
while b" 001 " not in b: b += s.recv(4096)
w("PRIVMSG NickServ :REGISTER pw12345 *")
while b"Account created" not in b: b += s.recv(4096)
w("QUIT")
EOF
start
kill $app; wait $app 2>/dev/null || true
printf 'v=2.16.2\n\nN=Lab\nF=0\nD=0\nS=127.0.0.1/%s\n\n' "$ERGO_PORT" > "$tmp/cfg/servlist.conf"
DISPLAY=$disp NEXCHAT_TABS_DUMP=1 NEXCHAT_BNC_SETUP=1 "$bin" -a -d "$tmp/cfg" >"$tmp/log" 2>&1 &
app=$!
until_state bncsetup 'bncsetup: choose*' || true
choose alt+k 'bncsetup: persist*'
until_state bncsetup "bncsetup: persist*can keep you online*" && ok "ergo: the network is checked when picked" ||
	{ bad "ergo: check"; state bncsetup; }
k alt+y; t "owner"; k alt+s; t "pw12345"
until_state bncsetup "*next=yes" && ok "ergo: account given, Connect allowed" || bad "ergo: connect"
k Return
sleep 4
grep -A6 '^N=Lab$' "$tmp/cfg/servlist.conf" | grep -q '^L=6$' && ok "ergo: logs in with the account (SASL)" || bad "ergo: login"
grep -q 'always-on' "$tmp/lab/ergo/ergo.log" 2>/dev/null || true
until_state title "*online 24/7*" && ok "ergo: always-on turned on, the title says so" || { bad "ergo: title"; state title; }

# ---- explain
start
choose alt+w 'bncsetup: explain*'
until_state bncsetup "bncsetup: explain*" && ok "explain: a page of its own" || bad "explain"

[ $fail -eq 0 ] || exit 1
echo "stayonline: ok"
exit 0

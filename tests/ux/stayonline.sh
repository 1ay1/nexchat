#!/bin/sh
# "Stay online" (Network List), against real soju, ZNC and Ergo: pick a
# choice, fill in the fields with the keyboard, Check says what it found,
# Save and connect writes the network entry and connects through it.
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
trap 'st=$?; kill $app $wm 2>/dev/null; bnc_stop; kill $xv 2>/dev/null; rm -rf "$tmp"; exit $st' EXIT
start_xvfb "$xvfb" "$disp"
# the dialog is a separate window: focus between them needs a WM
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
	until_state bncsetup '*none*' || { bad "dialog didn't open"; return 1; }
}

# ---- soju: a bouncer, the right password
start
# Alt+B: "I have a bouncer"; the address field is next
k alt+b
until_state bncsetup 'bncsetup: bouncer*' && ok "soju: bouncer fields shown" || bad "soju: choice"
k alt+a; t "127.0.0.1"
k alt+p; t "$SOJU_PORT"
k alt+t	# TLS off (on by default)
k alt+u; t "ayush"
k alt+w; t "secret123"
k alt+h
until_state bncsetup "*That works*soju*" && ok "soju: Check says it's soju and the password works" ||
	{ bad "soju: check"; state bncsetup; }
k alt+s
sleep 3
grep -q "^S=127.0.0.1/$SOJU_PORT$" "$tmp/cfg/servlist.conf" && ok "soju: saved as a network" || { bad "soju: not saved"; cat "$tmp/cfg/servlist.conf"; }
grep -q '^U=ayush$' "$tmp/cfg/servlist.conf" && grep -q '^L=7$' "$tmp/cfg/servlist.conf" &&
	ok "soju: logs in with its account (server password)" || bad "soju: login fields"
until_state tab "tab: *127.0.0.1*" || until_state tab "tab: localhost*" || true
grep -q "starting bouncer\|registration complete for user \"ayush\"" "$tmp/lab/soju/soju.log" &&
	ok "soju: connected through it" || bad "soju: no login on soju"
until_state title "*online 24/7*" && ok "soju: the title says online 24/7" || { bad "soju: title"; state title; }

# ---- soju over TLS with a self-signed certificate: said, and only
# trusted when you tick the box
start
k alt+b; k alt+a; t "127.0.0.1"; k alt+p; t "$SOJU_TLS_PORT"
k alt+u; t "ayush"; k alt+w; t "secret123"
k alt+h
until_state bncsetup "*That works*soju*Careful*self-signed*" && ok "tls: a self-signed certificate is pointed out" ||
	{ bad "tls: no certificate warning"; state bncsetup; }
k alt+y; k alt+s
sleep 3
flags=$(awk -v s="S=127.0.0.1/+$SOJU_TLS_PORT" '/^N=/{f=""} /^F=/{f=$0} $0==s{print f}' "$tmp/cfg/servlist.conf" | cut -d= -f2)
[ -n "$flags" ] && [ $(( flags & 32 )) -ne 0 ] && [ $(( flags & 4 )) -ne 0 ] &&
	ok "tls: ticked, saved with TLS and that certificate allowed" || { bad "tls: flags $flags"; cat "$tmp/cfg/servlist.conf"; }
# the same without ticking: TLS, but certificates still checked
start
k alt+b; k alt+a; t "127.0.0.1"; k alt+p; t "$SOJU_TLS_PORT"
k alt+u; t "ayush"; k alt+w; t "secret123"
k alt+h
until_state bncsetup "*Careful*" || true
k alt+s
sleep 3
flags=$(awk -v s="S=127.0.0.1/+$SOJU_TLS_PORT" '/^N=/{f=""} /^F=/{f=$0} $0==s{print f}' "$tmp/cfg/servlist.conf" | cut -d= -f2)
[ -n "$flags" ] && [ $(( flags & 32 )) -eq 0 ] && ok "tls: not ticked, certificates still checked" || bad "tls: allowed without asking ($flags)"

# ---- soju: a wrong password is caught by Check
start
k alt+b; k alt+a; t "127.0.0.1"; k alt+p; t "$SOJU_PORT"; k alt+t
k alt+u; t "ayush"; k alt+w; t "nope"
k alt+h
until_state bncsetup "*password is wrong*" && ok "soju: a wrong password is said plainly" || { bad "soju: wrong password"; state bncsetup; }

# ---- ZNC
start
k alt+b; k alt+a; t "127.0.0.1"; k alt+p; t "$ZNC_PORT"; k alt+t
k alt+u; t "ayush"; k alt+w; t "secret123"
k alt+h
until_state bncsetup "*That works*ZNC*" && ok "znc: Check says it's ZNC" || { bad "znc: check"; state bncsetup; }

# ---- the network itself (Ergo): an account, always-on turned on after login
python3 - "$ERGO_PORT" <<'EOF'
import socket, sys, time
s = socket.create_connection(("127.0.0.1", int(sys.argv[1]))); f = s.makefile("rwb")
def w(l): f.write((l + "\r\n").encode()); f.flush()
w("NICK owner"); w("USER owner 0 * :o"); time.sleep(0.8)
w("PRIVMSG NickServ :REGISTER pw12345 *"); time.sleep(0.8); w("QUIT"); time.sleep(0.2)
EOF
start
k alt+k
until_state bncsetup 'bncsetup: network*' && ok "ergo: network fields shown" || bad "ergo: choice"
k alt+d; t "127.0.0.1"; k alt+o; t "$ERGO_PORT"; k alt+l
k alt+h
until_state bncsetup "*this network can keep you online*" && ok "ergo: Check says the network can do it" || { bad "ergo: check"; state bncsetup; }
k alt+n; t "owner"; k alt+r; t "pw12345"
k alt+s
sleep 4
grep -q "^S=127.0.0.1/$ERGO_PORT$" "$tmp/cfg/servlist.conf" && ok "ergo: saved" || bad "ergo: not saved"

# ---- "explain"
start
k alt+e
until_state bncsetup "*bouncer is a small program*" && ok "explain: says what a bouncer is" || bad "explain"

[ $fail -eq 0 ] || exit 1
echo "stayonline: ok"

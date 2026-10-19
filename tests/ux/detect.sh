#!/bin/sh
# "Stay online" with soju installed on this computer (the lab's soju,
# found through NEXCHAT_SOJU_DIR): the first page says soju is here, one
# click sets up nexchat's own soju, your networks are listed with the ones
# you use ticked, Connect moves them and connects through it. Nothing typed.
# Also: a bouncer already running here is found and offered.
# usage: tests/ux/detect.sh path/to/nexchat   (Xvfb + XTest + openbox)
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../bnc/lab.sh"
bnc_have || { echo "detect: bouncers not fetched (tests/bnc/fetch.sh), skipped"; exit 77; }
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:78}
tmp=$(mktemp -d)
app=; xv=; wm=
cleanup() {
	[ -n "$app" ] && kill $app 2>/dev/null
	[ -n "$wm" ] && kill $wm 2>/dev/null
	pkill -f "soju -config $tmp/cfg/bouncer/config" 2>/dev/null
	bnc_stop; kill $xv 2>/dev/null; rm -rf "$tmp"
}
trap 'st=$?; cleanup; exit $st' EXIT
start_xvfb "$xvfb" "$disp"
ob=${OPENBOX:-$(command -v openbox || true)}
obref=$HOME/.cache/nexchat-ref/ob/usr
[ -z "$ob" ] && [ -x "$obref/bin/openbox" ] && ob=$obref/bin/openbox
[ -n "$ob" ] || { echo "detect: no openbox, skipped"; exit 77; }
DISPLAY=$disp LD_LIBRARY_PATH=$obref/lib XDG_CONFIG_DIRS=$obref/../etc/xdg XDG_DATA_DIRS=$obref/share "$ob" --sm-disable >/dev/null 2>&1 &
wm=$!
bnc_start "$tmp/lab"
# soju "installed" (the lab's), run by nexchat itself, not systemd
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
until_state() { i=0; while [ $i -lt 30 ]; do case "$(state "$1")" in $2) return 0;; esac; i=$((i+1)); sleep 0.5; done; return 1; }
press_until() { # KEY PATTERN: the first key can come before the window has focus
	n=0
	while [ $n -lt 5 ]; do
		k $1
		j=0; while [ $j -lt 6 ]; do case "$(state bncsetup)" in $2) return 0;; esac; j=$((j+1)); sleep 0.5; done
		n=$((n+1))
	done
	return 1
}

# a network as people have it: on the lab's Ergo, a channel, a favourite
mkdir -p "$tmp/cfg"
printf 'gui_slist_skip = 1\nirc_nick1 = dee\nnet_throttle = 0\n' > "$tmp/cfg/hexchat.conf"
printf 'v=2.16.2\n\nN=Lab\nI=dee\nF=64\nD=0\nS=127.0.0.1/%s\nJ=#den\n\nN=Unused\nF=0\nD=0\nS=irc.example.invalid\n\n' "$ERGO_PORT" > "$tmp/cfg/servlist.conf"
DISPLAY=$disp NEXCHAT_TABS_DUMP=1 NEXCHAT_BNC_SETUP=1 "$bin" -a -d "$tmp/cfg" >"$tmp/log" 2>&1 &
app=$!

until_state bncsetup 'bncsetup: choose*' || bad "window"
# the found list has its own dump line: wait for "soju ... installed"
i=0; while [ $i -lt 30 ] && ! grep -q '^bncfound:.*soju.*is installed on this computer' "$tmp/log"; do sleep 0.5; i=$((i+1)); done
grep -q '^bncfound:.*soju.*is installed on this computer' "$tmp/log" && ok "soju on this computer is found and offered" ||
	{ bad "not offered"; grep '^bncfound' "$tmp/log" | tail -1; }
press_until alt+u 'bncsetup: networks*' && ok "one click: straight to the networks, nothing to type" || { bad "no networks page"; state bncsetup; }
grep -q '^bncnets:.*\[x\] Lab' "$tmp/log" && ok "the network you use is ticked" || { bad "Lab not ticked"; grep '^bncnets' "$tmp/log" | tail -1; }
grep -q '^bncnets:.*\[ \] Unused' "$tmp/log" && ok "one you don't use isn't" || bad "Unused ticked"
k alt+n
until_state title "*online 24/7*" && ok "connected through it: online 24/7" || { bad "title"; state title; }
until_state tab "tab: #den*" && ok "in its channel, through soju" || { bad "no #den tab"; state tab; }
pgrep -f "soju -config $tmp/cfg/bouncer/config" >/dev/null && ok "nexchat's own soju is running" || bad "soju not running"
port=$(cat "$tmp/cfg/bouncer/port")
grep -A10 '^N=Lab$' "$tmp/cfg/servlist.conf" | grep -q "^S=127.0.0.1/$port$" && ok "the Lab entry goes through it now" || bad "entry not moved"

# a second time: it's already there, and a bouncer listening here is found
kill $app; wait $app 2>/dev/null || true
DISPLAY=$disp NEXCHAT_TABS_DUMP=1 NEXCHAT_BNC_SETUP=1 NEXCHAT_DETECT_PORTS="$ZNC_PORT" "$bin" -a -d "$tmp/cfg" >"$tmp/log" 2>&1 &
app=$!
i=0; while [ $i -lt 30 ] && ! grep -q '^bncfound:.*already runs a soju' "$tmp/log"; do sleep 0.5; i=$((i+1)); done
grep -q '^bncfound:.*already runs a soju' "$tmp/log" && ok "next time: says it already runs one" || { bad "not recognised as ours"; grep '^bncfound' "$tmp/log" | tail -1; }
grep -q "^bncfound:.*A ZNC running here, on port $ZNC_PORT" "$tmp/log" && ok "a ZNC running here is found too" || { bad "ZNC not found"; grep '^bncfound' "$tmp/log" | tail -1; }

# opened from the moved network (Server > Stay Online on its tab): the
# start page, not a form with the bouncer's address and a TLS error
kill $app; wait $app 2>/dev/null || true
DISPLAY=$disp NEXCHAT_TABS_DUMP=1 NEXCHAT_BNC_SETUP=Lab "$bin" -a -d "$tmp/cfg" >"$tmp/log" 2>&1 &
app=$!
sleep 4
until_state bncsetup 'bncsetup: choose*' && ok "from a moved network: the start page, nothing to fill in" || { bad "form shown"; state bncsetup; }
grep -q "TLS" "$tmp/log" && bad "a TLS error came up" || ok "no TLS error"

[ $fail -eq 0 ] || exit 1
echo "detect: ok"
exit 0

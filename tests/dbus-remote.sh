#!/bin/sh
# The D-Bus remote API (what remote scripts and `nexchat -e` use): start
# nexchat on a private session bus, connected to fake_ircd, and drive it
# with tests/dbus-remote.py. Then `nexchat -e -c "say ..."` from a second
# process must reach the running one. Skipped (77) without a D-Bus build,
# dbus-run-session or python3-gi.
# usage: tests/dbus-remote.sh BIN SERVICE     (needs Xvfb)
set -eu
bin=$1; service=$2
here=$(cd "$(dirname "$0")" && pwd)
command -v dbus-run-session >/dev/null || { echo "no dbus-run-session, skipped"; exit 77; }
python3 -c 'from gi.repository import Gio' 2>/dev/null || { echo "no python3-gi, skipped"; exit 77; }

if [ -z "${NX_IN_BUS:-}" ]; then
	NX_IN_BUS=1 exec dbus-run-session -- sh "$0" "$@"
fi

NX_UX_DIR=$here/ux
. "$here/ux/isolate.sh"
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:89}
tmp=$(mktemp -d)
app=; ircd=; xv=
cleanup() { kill $app $ircd $xv 2>/dev/null || true; wait 2>/dev/null || true
	if [ -n "${KEEP:-}" ]; then cp -r "$tmp" "$KEEP"; fi; rm -rf "$tmp"; }
trap cleanup EXIT
start_xvfb "$xvfb" "$disp"

python3 "$here/fake_ircd.py" --log "$tmp/server.log" --portfile "$tmp/port" --timeout 60 &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = remote\ngui_slist_skip = 1\n' > "$tmp/cfg/hexchat.conf"
DISPLAY=$disp "$bin" -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/#remote" >"$tmp/log" 2>&1 &
app=$!
i=0; while ! grep -q '^JOIN #remote' "$tmp/server.log" 2>/dev/null && [ $i -lt 150 ]; do sleep 0.1; i=$((i+1)); done
# the plugin owns the name once it's loaded
i=0; while ! gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
		--method org.freedesktop.DBus.NameHasOwner "$service" 2>/dev/null | grep -q true && [ $i -lt 50 ]; do
	sleep 0.1; i=$((i+1)); done
sleep 1

fail=0
python3 "$here/dbus-remote.py" "$service" "$tmp/server.log" || fail=1

# a second nexchat with -e hands its command to the running one and exits
# (it runs in the front tab, so name the channel)
DISPLAY=$disp timeout 10 "$bin" -d "$tmp/cfg" -e -c "msg #remote via existing" >"$tmp/log2" 2>&1 \
	&& ok=1 || ok=0
sleep 1
if [ $ok = 1 ] && grep -q '^PRIVMSG #remote :via existing' "$tmp/server.log"; then
	echo "ok:   -e -c reaches the running nexchat"
else
	echo "FAIL: -e -c (exit ok=$ok)"; tail -5 "$tmp/log2"; fail=1
fi
kill -0 "$app" 2>/dev/null || { echo "FAIL: nexchat died"; tail -20 "$tmp/log"; fail=1; }
if grep -q CRITICAL "$tmp/log"; then echo "FAIL: criticals"; grep CRITICAL "$tmp/log" | head; fail=1; fi
[ $fail -eq 0 ] && echo "dbus-remote.sh: ok" || exit 1

#!/bin/sh
# Run a program on a private headless weston (Wayland). usage: with-weston.sh PROG [ARGS]
# WESTON points at the weston binary; a weston unpacked outside /usr works
# too, its modules are found next to it.
set -eu
NX_UX_DIR=$(cd "$(dirname "$0")" && pwd)
. "$NX_UX_DIR/isolate.sh"
weston=${WESTON:-$(command -v weston)}
prefix=$(cd "$(dirname "$weston")/.." && pwd)
lib=$prefix/lib
if [ "$prefix" != /usr ]; then
	export LD_LIBRARY_PATH=$lib:$lib/weston${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
	map=
	for m in "$lib"/libweston-*/*.so "$lib"/weston/*.so; do
		map="$map$(basename "$m")=$m;"
	done
	export WESTON_MODULE_MAP=$map
fi
rt=$(mktemp -d)
chmod 700 "$rt"
export XDG_RUNTIME_DIR=$rt
sock=nx-wayland
client=$lib/weston/weston-desktop-shell
# unpacked weston: point at its client; a system one knows where its own is
clientline=
[ "$prefix" != /usr ] && [ -x "$client" ] && clientline="client=$client"
printf '[core]\nidle-time=0\n[shell]\n%s\npanel-position=none\nbackground-color=0xff808080\nlocking=false\nanimation=none\nstartup-animation=none\n' \
	"$clientline" >"$rt/weston.ini"
# GTK3 needs a seat. NX_NO_SEAT=1 runs without one, so nothing gets focus.
# --fake-seat is weston 14+; older ones (Ubuntu's 13) refuse it. GTK4
# copes without a seat there; GTK3 doesn't (NX_NEEDS_SEAT=1: skip)
seat=
if [ -z "${NX_NO_SEAT:-}" ]; then
	if "$weston" --help 2>&1 | grep -q -- '--fake-seat'; then
		seat=--fake-seat
	elif [ -n "${NX_NEEDS_SEAT:-}" ]; then
		echo "skipped: this weston has no --fake-seat (needs 14+)"
		exit 77
	fi
fi
"$weston" --backend=headless --shell=desktop --socket=$sock $seat \
	--width=1280 --height=800 --config="$rt/weston.ini" >"$rt/weston.log" 2>&1 &
wp=$!
trap 'kill $wp 2>/dev/null || true; wait $wp 2>/dev/null || true; rm -rf "$rt"' EXIT
i=0; while [ ! -S "$rt/$sock" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
[ -S "$rt/$sock" ] || { echo "weston did not start"; cat "$rt/weston.log"; exit 1; }
unset DISPLAY
# GTK on Wayland reads theme and fonts from the portal/gsettings, not
# settings.ini: keep the desktop's out with a private bus and memory gsettings
export GSETTINGS_BACKEND=memory GIO_USE_VFS=local GDK_DEBUG=no-portals
export NX_WAYLAND=1 NX_IN_BUS=1
cat >"$rt/bus.conf" <<EOF
<busconfig><type>session</type><listen>unix:dir=$rt</listen>
<policy context="default"><allow send_destination="*" eavesdrop="true"/>
<allow eavesdrop="true"/><allow own="*"/></policy></busconfig>
EOF
WAYLAND_DISPLAY=$sock GDK_BACKEND=wayland \
	dbus-run-session --config-file="$rt/bus.conf" -- "$@"

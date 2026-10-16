#!/bin/sh
# Run a program on a private headless sway (wlroots). usage: with-sway.sh PROG [ARGS]
# SWAY points at the sway binary; one unpacked outside /usr finds its libs
# next to it. Sets SWAYSOCK for swaymsg.
set -eu
NX_UX_DIR=$(cd "$(dirname "$0")" && pwd)
. "$NX_UX_DIR/isolate.sh"
sway=${SWAY:-$(command -v sway)}
prefix=$(cd "$(dirname "$sway")/.." && pwd)
[ "$prefix" != /usr ] && export LD_LIBRARY_PATH=$prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export PATH=$prefix/bin:$PATH
rt=$(mktemp -d)
chmod 700 "$rt"
export XDG_RUNTIME_DIR=$rt
printf 'output HEADLESS-1 resolution 1280x800\n' >"$rt/config"
unset DISPLAY WAYLAND_DISPLAY
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 \
	"$sway" -c "$rt/config" >"$rt/sway.log" 2>&1 &
sp=$!
trap 'kill $sp 2>/dev/null || true; wait $sp 2>/dev/null || true; rm -rf "$rt"' EXIT
i=0; while ! ls "$rt"/wayland-* >/dev/null 2>&1 && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
sock=$(cd "$rt" && ls wayland-* 2>/dev/null | grep -v lock | head -1)
[ -n "$sock" ] || { echo "sway did not start"; cat "$rt/sway.log"; exit 1; }
export SWAYSOCK=$(ls "$rt"/sway-ipc.*.sock)
export GSETTINGS_BACKEND=memory GIO_USE_VFS=local GDK_DEBUG=no-portals
export NX_WAYLAND=1 NX_IN_BUS=1
cat >"$rt/bus.conf" <<EOF
<busconfig><type>session</type><listen>unix:dir=$rt</listen>
<policy context="default"><allow send_destination="*" eavesdrop="true"/>
<allow eavesdrop="true"/><allow own="*"/></policy></busconfig>
EOF
WAYLAND_DISPLAY=$sock GDK_BACKEND=wayland \
	dbus-run-session --config-file="$rt/bus.conf" -- "$@"

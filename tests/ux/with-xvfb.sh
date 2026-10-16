#!/bin/sh
# Run a program on a private Xvfb display. usage: with-xvfb.sh PROG [ARGS]
set -eu
. "$(dirname "$0")/isolate.sh"
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:84}
start_xvfb "$xvfb" "$disp" 800 600
trap 'kill $xv 2>/dev/null || true' EXIT
sleep 1.5
DISPLAY=$disp "$@"

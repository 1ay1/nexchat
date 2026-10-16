#!/bin/sh
# Run a program on a private Xvfb display. usage: with-xvfb.sh PROG [ARGS]
set -eu
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:84}
"$xvfb" "$disp" -screen 0 800x600x24 -nolisten tcp >/dev/null 2>&1 &
xv=$!
trap 'kill $xv 2>/dev/null || true' EXIT
sleep 1.5
DISPLAY=$disp "$@"

#!/bin/sh
# Preferences on one page, shot by the app itself (no xwd).
# usage: tests/ux/prefs-shot.sh BIN OUT.png [PAGE [SCHEME]]
#   PAGE: 1 Input box, 4 Colors, ...  env: CLASSIC=0 to see GTK's own look
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1; out=$2; page=${3:-1}; scheme=${4:-}
tmp=$(mktemp -d)
xv=
trap 'kill $xv 2>/dev/null || true; rm -rf "$tmp"' EXIT
start_xvfb "$(command -v Xvfb)" "${DISP:-:61}" 1100 760
export DISPLAY=${DISP:-:61}
mkdir "$tmp/cfg"
printf 'gui_slist_skip = 1\nirc_nick1 = yo\ngui_input_cursor = 2\n' > "$tmp/cfg/hexchat.conf"
[ -n "$scheme" ] && printf 'scheme = %s\n' "$scheme" > "$tmp/cfg/colors.conf"
NEXCHAT_MENU_ACTIVATE="Settings/Preferences" NEXCHAT_PREFS_PAGE=$page \
NEXCHAT_SHOT_WIN=1 NEXCHAT_SHOT="$out:4" NX_DUMP_CSS=${NX_DUMP_CSS:-} \
	timeout 7 "$bin" -n -d "$tmp/cfg" >"$tmp/log" 2>&1 || true
[ -n "${LOG:-}" ] && cp "$tmp/log" "$LOG"
[ -s "$out" ] || { echo "no shot"; tail -5 "$tmp/log"; exit 1; }

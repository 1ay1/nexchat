#!/bin/sh
# The docs/ux-reference/gtk2/main-tree.png scene (fake ircd --demo, same
# config), rendered by BIN with NEXCHAT_SHOT on whatever backend it has --
# the native one on macOS/Windows, no X server needed. For comparing the
# port against HexChat by eye after a change.
# usage: tests/ux/mac-scene.sh BIN OUT.png [extra hexchat.conf lines]
set -eu
bin=$1 out=$2 extra=${3:-}
here=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'kill $srv 2>/dev/null || true; if [ -n "${KEEP:-}" ]; then cp -r "$tmp" "$KEEP"; fi; rm -rf "$tmp"' EXIT

python3 "$here/tests/fake_ircd.py" --demo --timeout 60 \
	--log "$tmp/server.log" --portfile "$tmp/port" &
srv=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
port=$(cat "$tmp/port")

mkdir "$tmp/cfg"
# CFG=dir starts from a real config (colours, fonts, prefs), the scene's
# settings on top
if [ -n "${CFG:-}" ]; then
	for f in hexchat.conf colors.conf pevents.conf; do
		if [ -f "$CFG/$f" ]; then cp "$CFG/$f" "$tmp/cfg/"; fi
	done
	cp "$tmp/cfg/hexchat.conf" "$tmp/base.conf" 2>/dev/null || true
fi
# the first line for a name wins: the scene's settings, then the base
{ printf '%s\n' "irc_nick1 = ayush" "irc_user_name = ayush" "irc_real_name = Ayush" \
	"net_throttle = 0" "gui_slist_skip = 1" "gui_join_dialog = 0" \
	"gui_win_width = 1000" "gui_win_height = 640" "gui_win_save = 1" \
	"gui_quit_dialog = 0" "gui_tray = 0" \
	"stamp_text = 1" "stamp_text_format = [12:34]"
if [ -n "$extra" ]; then printf '%s\n' "$extra"; fi
if [ -s "$tmp/base.conf" ]; then cat "$tmp/base.conf"; fi; } > "$tmp/cfg/hexchat.conf"

rm -f "$out"
# CHANS="#a,#b": join these too (the last one ends up in front);
# NEXCHAT_SWITCH: after the zoom steps, go back to #nexchat
NEXCHAT_SWITCH="${SWITCH:-}" NEXCHAT_ZOOM="${ZOOM:-}" NEXCHAT_SCHEME="${SCHEME:-}" NEXCHAT_SHOT="$out:${SECS:-6}" "$bin" -n -d "$tmp/cfg" "irc://127.0.0.1:-$port/#nexchat${CHANS:+,$CHANS}" \
	>"$tmp/log" 2>&1 &
app=$!
sleep $(( ${SECS:-6} + 1 ))
kill $app 2>/dev/null || true
wait $app 2>/dev/null || true
grep -vE '^$|skipped frame' "$tmp/log" >&2 || true
test -s "$out"

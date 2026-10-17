#!/bin/sh
# The README screenshot: nexchat on tests/fake_ircd.py --showcase (a few
# channels with people, a conversation and real-looking timestamps), the
# window saved with NEXCHAT_SHOT in light and dark.
# usage: tests/ux/readme-shot.sh BIN OUTDIR   (writes OUTDIR/nexchat-{light,dark}.png)
set -eu
bin=$1 out=$2
here=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$out"

shot () {
	scheme=$1 file=$2
	tmp=$(mktemp -d)
	python3 "$here/tests/fake_ircd.py" --showcase --timeout 60 \
		--log "$tmp/server.log" --portfile "$tmp/port" &
	srv=$!
	i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
	mkdir "$tmp/cfg"
	# tree on the left with the network and its channels, timestamps,
	# nick colours, the user list with its mode icons and counts
	printf '%s\n' "irc_nick1 = ayush" "irc_user_name = ayush" "irc_real_name = Ayush" \
		"net_throttle = 0" "gui_slist_skip = 1" "gui_join_dialog = 0" \
		"gui_win_width = 1180" "gui_win_height = 560" "gui_win_save = 1" \
		"gui_quit_dialog = 0" "gui_tray = 0" "gui_tab_layout = 2" \
		"gui_pane_left_size = 150" "gui_pane_right_size = 140" \
		"gui_ulist_count = 1" "gui_ulist_icons = 1" "text_color_nicks = 1" \
		"stamp_text = 1" "stamp_text_format = %H:%M" "text_font = Menlo 13" \
		"text_max_indent = 220" > "$tmp/cfg/hexchat.conf"
	rm -f "$file"
	# #gtk and #linux first, #nexchat last so it's the one in front
	NEXCHAT_SHOT_SCALE=2 NEXCHAT_SCHEME=$scheme NEXCHAT_SHOT="$file:6" \
		"$bin" -n -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/#gtk,#linux,#nexchat" \
		>"$tmp/log" 2>&1 &
	app=$!
	sleep 7
	kill $app $srv 2>/dev/null || true
	wait $app 2>/dev/null || true
	rm -rf "$tmp"
	test -s "$file"
}

# the first start after a build sometimes misses the shot: try again
for s in light dark; do
	shot $s "$out/nexchat-$s.png" || shot $s "$out/nexchat-$s.png"
	echo "$out/nexchat-$s.png"
done

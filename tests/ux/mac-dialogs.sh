#!/bin/sh
# Every dialog the GTK2 reference pack has, opened through its real menu
# item (NEXCHAT_MENU_ACTIVATE) and saved with NEXCHAT_SHOT_WIN, on the
# build's own backend -- native on macOS, no X server. Connected to
# tests/fake_ircd.py --demo so the lists aren't empty.
# usage: tests/ux/mac-dialogs.sh BIN OUTDIR [name ...]
#   CFG=dir    start from a real config (colours, fonts, dark mode prefs)
set -eu
bin=$1 out=$2; shift 2
here=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$out"

# name|menu path (as menu.c labels it, mnemonics stripped)
list='network-list|nexchat/Network List
preferences-00|Settings/Preferences
auto-replace|Settings/Auto Replace
ctcp-replies|Settings/CTCP Replies
dialog-buttons|Settings/Dialog Buttons
keyboard-shortcuts|Settings/Keyboard Shortcuts
text-events|Settings/Text Events
url-handlers|Settings/URL Handlers
user-commands|Settings/User Commands
userlist-buttons|Settings/User List Buttons
userlist-popup|Settings/User List Popup
ban-list|Window/Ban List
character-chart|Window/Character Chart
direct-chat|Window/Direct Chat
file-transfers|Window/File Transfers
friends-list|Window/Friends List
ignore-list|Window/Ignore List
plugins|Window/Plugins and Scripts
raw-log|Window/Raw Log
url-grabber|Window/URL Grabber
join|Server/Join a Channel
channel-list|Server/Channel List
about|Help/About'

echo "$list" | while IFS='|' read -r name path; do
	if [ $# -gt 0 ]; then
		case " $* " in *" $name "*) ;; *) continue ;; esac
	fi
	tmp=$(mktemp -d)
	python3 "$here/tests/fake_ircd.py" --demo --timeout 30 \
		--log "$tmp/server.log" --portfile "$tmp/port" &
	srv=$!
	i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
	mkdir "$tmp/cfg"
	{ printf '%s\n' "irc_nick1 = ayush" "net_throttle = 0" "gui_slist_skip = 1" \
		"gui_join_dialog = 0" "gui_win_width = 1000" "gui_win_height = 640" \
		"gui_quit_dialog = 0" "gui_tray = 0" "stamp_text_format = [12:34]"
	  if [ -n "${CFG:-}" ] && [ -f "$CFG/hexchat.conf" ]; then cat "$CFG/hexchat.conf"; fi
	} > "$tmp/cfg/hexchat.conf"
	if [ -n "${CFG:-}" ] && [ -f "$CFG/colors.conf" ]; then cp "$CFG/colors.conf" "$tmp/cfg/"; fi
	rm -f "$out/$name.png"
	NEXCHAT_MENU_ACTIVATE="$path" NEXCHAT_SHOT_WIN=1 NEXCHAT_SHOT="$out/$name.png:5" \
		"$bin" -n -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/#nexchat" >"$tmp/log" 2>&1 &
	app=$!
	sleep 6
	kill $app $srv 2>/dev/null || true
	wait $app 2>/dev/null || true
	if [ -s "$out/$name.png" ]; then echo "$name: ok"; else echo "$name: NO SHOT"; fi
	grep -vE '^$|skipped frame' "$tmp/log" | sed "s/^/  $name: /" | head -5 || true
	rm -rf "$tmp"
done

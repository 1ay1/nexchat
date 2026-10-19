#!/bin/sh
# Shots of the main window, Preferences and the Network List in one colour
# scheme, for checking that the whole UI follows it.
# usage: tests/ux/scheme-shots.sh BIN OUTDIR SCHEME [light|dark]
set -eu
bin=$1; out=$2; scheme=$3; mode=${4:-}
here=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$out"
export GTK_A11Y=none G_DEBUG=fatal-criticals
[ -n "$mode" ] && export NEXCHAT_SCHEME=$mode
slug=$(printf '%s' "$scheme" | tr -c 'A-Za-z0-9' '_')
shot () # name menu-path [extra env]
{
	tmp=$(mktemp -d)
	python3 "$here/tests/fake_ircd.py" --demo --timeout 20 --log "$tmp/s.log" --portfile "$tmp/port" &
	srv=$!
	i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
	mkdir "$tmp/cfg"
	printf 'gui_slist_skip = 1\nirc_nick1 = shot\ngui_tab_layout = 0\ngui_tab_pos = 6\ngui_win_width = 900\ngui_win_height = 560\n' > "$tmp/cfg/hexchat.conf"
	printf 'scheme = %s\n' "$scheme" > "$tmp/cfg/colors.conf"
	if [ -n "$2" ]; then
		env ${3:-} NEXCHAT_MENU_ACTIVATE="$2" NEXCHAT_SHOT_WIN=1 NEXCHAT_SHOT="$out/$slug-$1.png:4" \
			"$bin" -n -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/#nexchat,#help" >"$tmp/log" 2>&1 &
	else
		NEXCHAT_SHOT="$out/$slug-$1.png:4" \
			"$bin" -n -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/#nexchat,#help" >"$tmp/log" 2>&1 &
	fi
	app=$!
	sleep 6
	kill $app $srv 2>/dev/null || true
	st=0; wait $app 2>/dev/null || st=$?
	if grep -q CRITICAL "$tmp/log"; then echo "$1: CRITICAL"; grep CRITICAL "$tmp/log" | head -3; fi
	[ -s "$out/$slug-$1.png" ] && echo "$1: ok" || echo "$1: NO SHOT (exit $st)"
	rm -rf "$tmp"
}
shot main ""
shot prefs "Settings/Preferences" "NEXCHAT_PREFS_PAGE=4"
shot servlist "nexchat/Network List"

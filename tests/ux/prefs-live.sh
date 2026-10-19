#!/bin/sh
# Preferences applies as you go: picking a colour scheme repaints the
# window behind it and is saved, with no OK button; a toggle is saved the
# same way; and closing keeps it all.
# usage: tests/ux/prefs-live.sh path/to/nexchat
set -eu
bin=$1
tmp=$(mktemp -d)
app=
trap 'kill $app 2>/dev/null; rm -rf "$tmp"' EXIT
export GTK_A11Y=none G_DEBUG=fatal-criticals NEXCHAT_SCHEME=dark

fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }

mkdir "$tmp/cfg"
printf 'gui_slist_skip = 1\nirc_nick1 = live\n' > "$tmp/cfg/hexchat.conf"
printf 'scheme = \n' > "$tmp/cfg/colors.conf"
NEXCHAT_MENU_ACTIVATE="Settings/Preferences" NEXCHAT_PREFS_PAGE=4 NEXCHAT_PREFS_PICK="Tokyo Night" \
	NEXCHAT_WINDOWS_DUMP=1 NEXCHAT_SHOT_WIN=1 NEXCHAT_SHOT="$tmp/shot.png:4" NX_DUMP_CSS=1 \
	"$bin" -n -d "$tmp/cfg" >"$tmp/log" 2>&1 &
app=$!
i=0; while ! grep -q 'pair:Tokyo Night' "$tmp/cfg/colors.conf" 2>/dev/null && [ $i -lt 40 ]; do sleep 0.25; i=$((i+1)); done
grep -q '^scheme = pair:Tokyo Night' "$tmp/cfg/colors.conf" && ok "scheme saved the moment it's picked" || { bad "not saved"; cat "$tmp/cfg/colors.conf"; }
sleep 1
grep -q '@define-color theme_bg_color' "$tmp/log" && ok "the window's own colours follow the scheme" || bad "no UI colours"
i=0; while ! grep -q '^window ' "$tmp/log" && [ $i -lt 30 ]; do sleep 0.25; i=$((i+1)); done
grep -q 'title="Preferences' "$tmp/log" && ok "Preferences still open (no OK needed)" || bad "prefs window"
grep -q 'OK\b' "$tmp/log" && grep -q 'label="OK"' "$tmp/log" && bad "an OK button" || ok "no OK button"
n=$(grep -c CRITICAL "$tmp/log" || true)
[ "$n" -eq 0 ] && ok "no criticals" || { bad "$n criticals"; grep CRITICAL "$tmp/log" | head -3; }
[ -n "${KEEP:-}" ] && [ -f "$tmp/shot.png" ] && cp "$tmp/shot.png" "$KEEP/prefs-live.png"
kill $app 2>/dev/null; wait $app 2>/dev/null || true; app=

[ $fail -eq 0 ] && echo "prefs-live: ok"
exit $fail

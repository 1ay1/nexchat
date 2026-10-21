#!/bin/sh
# Preferences applies as you go: picking a colour scheme repaints the
# window behind it and is saved, with no OK button; a toggle is saved the
# same way; and closing keeps it all.
# usage: tests/ux/prefs-live.sh path/to/nexchat
set -eu
bin=$1
tmp=$(mktemp -d)
app=
trap 'st=$?; [ -n "$app" ] && kill $app 2>/dev/null; rm -rf "$tmp"; exit $st' EXIT
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
kill $app 2>/dev/null || true; wait $app 2>/dev/null || true; app=

# arrow keys on the closed picker step through schemes, each applied
# and saved; Home/End go to the ends; Up at the top stays there
keys_run () # "keys" -> prints the picked label | saved id
{
	rm -rf "$tmp/k"; mkdir "$tmp/k"
	printf 'gui_slist_skip = 1\nirc_nick1 = live\n' > "$tmp/k/hexchat.conf"
	printf 'scheme = \n' > "$tmp/k/colors.conf"
	NEXCHAT_MENU_ACTIVATE="Settings/Preferences" NEXCHAT_PREFS_PAGE=4 NEXCHAT_PREFS_KEYS="$1" \
		"$bin" -n -d "$tmp/k" >"$tmp/k.log" 2>&1 &
	app=$!
	i=0; while ! grep -q '^prefs-scheme:' "$tmp/k.log" && [ $i -lt 40 ]; do sleep 0.25; i=$((i+1)); done
	sleep 0.6	# the live apply settles and saves
	kill $app 2>/dev/null || true; wait $app 2>/dev/null || true; app=
	sed -n 's/^prefs-scheme: //p' "$tmp/k.log" | tail -1
}
r=$(keys_run "Down")
case "$r" in Native*"| native") ok "Down: next scheme (native), applied";; *) bad "Down gave: $r";; esac
r=$(keys_run "Down Down Down")
case "$r" in *"(follows light/dark)"*"| pair:"*) ok "Down x3: a light/dark pair, applied ($r)";; *) bad "Down x3 gave: $r";; esac
grep -q '^scheme = pair:' "$tmp/k/colors.conf" && ok "the scheme reached with keys is saved" || bad "not saved: $(cat "$tmp/k/colors.conf")"
r=$(keys_run "Down Down Up")
case "$r" in Native*"| native") ok "Up goes back";; *) bad "Down Down Up gave: $r";; esac
r=$(keys_run "End")
case "$r" in *Zenbones*) ok "End: the last one";; *) bad "End gave: $r";; esac
r=$(keys_run "End Home")
case "$r" in *"| ") ok "Home: the first one (nexchat's own)";; *) bad "End Home gave: $r";; esac
r=$(keys_run "Up Up")
case "$r" in *"| ") ok "Up at the top stays";; *) bad "Up Up gave: $r";; esac
n=$(grep -c CRITICAL "$tmp/k.log" || true)
[ "$n" -eq 0 ] && ok "keys: no criticals" || bad "keys: $n criticals"

[ $fail -eq 0 ] && echo "prefs-live: ok"
exit $fail

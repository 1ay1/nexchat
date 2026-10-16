#!/bin/sh
# Spell checking in the input box: a misspelled word gets the right-click
# "Spelling Suggestions" submenu; choosing a suggestion replaces the word.
# Needs enchant with an en_US dictionary (macOS: AppleSpell; Linux: hunspell
# en_US); skipped (77) when none is found.
# usage: tests/ux/spell.sh BIN   (needs Xvfb + XTest)
set -eu
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
xwd=${XWD:-$(command -v xwd || echo /opt/X11/bin/xwd)}
disp=${DISP:-:77}
tmp=$(mktemp -d)
app=; xv=
cleanup() { kill $app $xv 2>/dev/null || true; wait 2>/dev/null || true
	if [ -n "${KEEP:-}" ]; then cp -r "$tmp" "$KEEP"; fi; rm -rf "$tmp"; }
trap cleanup EXIT

# enchant 2 from MacPorts / Homebrew on macOS
for d in /opt/local/lib /opt/homebrew/lib /usr/local/lib; do
	[ -e "$d/libenchant-2.dylib" ] && export DYLD_FALLBACK_LIBRARY_PATH="$d${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}"
done

"$xvfb" "$disp" -screen 0 1100x720x24 -nolisten tcp >/dev/null 2>&1 &
xv=$!
sleep 1.5
mkdir -p "$tmp/cfg"
printf 'gui_slist_skip = 1\nirc_nick1 = shot\ngui_win_left = 0\ngui_win_top = 0\ngui_win_width = 900\ngui_win_height = 600\ntext_spell_langs = en_US\ngui_input_spell = 1\n' > "$tmp/cfg/hexchat.conf"
DISPLAY=$disp G_MESSAGES_DEBUG=all "$bin" -a -d "$tmp/cfg" >"$tmp/log" 2>&1 &
app=$!
sleep 5
if ! grep -q "tag en_US" "$tmp/log"; then
	echo "spell: no enchant en_US dictionary, skipped"; exit 77
fi

x() { DISPLAY=$disp python3 "$here/xkey.py" --delay 0.6 --at 450,580 "$@"; }
shot() { DISPLAY=$disp "$xwd" -root -silent -out "$tmp/s.xwd"; python3 "$here/xwd2png.py" "$tmp/s.xwd" "$1"; }

x --type "wrold"
sleep 1
# right-click the word: Spelling Suggestions is the first entry; open it,
# then its first suggestion
x --rclick 190,580
sleep 1
[ -n "${SHOT:-}" ] && shot "$SHOT-menu.png"
x Down Right
sleep 1
[ -n "${SHOT:-}" ] && shot "$SHOT-sub.png"
x Return
sleep 1
# the replaced text, out through the clipboard: select all, copy, then
# check it via a /echo-free route: type it into a /set and read hexchat.conf
x ctrl+a ctrl+c
sleep 0.3
x ctrl+a BackSpace
x --type "/set -quiet text_spell_langs " ctrl+v Return
sleep 1
x --type "/set -quiet gui_input_spell 1" Return
sleep 1
got=$(sed -n 's/^text_spell_langs = //p' "$tmp/cfg/hexchat.conf")
kill -0 "$app" 2>/dev/null || { echo "spell: FAIL, client died"; exit 1; }
case "$got" in
	wrold|"") echo "spell: FAIL, word not replaced (got '$got')"; exit 1;;
	*) echo "spell: ok (wrold -> $got)";;
esac

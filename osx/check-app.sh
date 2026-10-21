#!/bin/sh
# Check a nexchat.app is self-contained: copy it somewhere else, run it
# with an empty environment, and fail if it loaded anything from outside
# the bundle and the system, or didn't get a window up.
# usage: osx/check-app.sh path/to/nexchat.app
set -eu
app=$1
tmp=$(mktemp -d)
trap 'kill $p 2>/dev/null || true; rm -rf "$tmp"' EXIT
cp -R "$app" "$tmp/"
mkdir "$tmp/cfg"
printf 'gui_slist_skip = 1\n' > "$tmp/cfg/hexchat.conf"
env -i HOME="$HOME" DYLD_PRINT_LIBRARIES=1 NEXCHAT_WINDOWS_DUMP=1 \
	NEXCHAT_SHOT="$tmp/shot.png:3" \
	"$tmp/nexchat.app/Contents/MacOS/nexchat" -d "$tmp/cfg" >"$tmp/log" 2>&1 &
p=$!
# the first start of a fresh bundle is slow on a CI runner (fonts,
# icon caches): wait for the window dump, up to 30 s
i=0; while ! grep -q '^window 0:' "$tmp/log" 2>/dev/null && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done
sleep 1
kill $p 2>/dev/null || true
wait $p 2>/dev/null || true

# realpath: /tmp is /private/tmp
bundle=$(cd "$tmp/nexchat.app" && pwd -P)
outside=$(grep -oE '^dyld\[[0-9]+\]: <[^>]*> /[^ ]+' "$tmp/log" | awk '{print $3}' |
	grep -vE "^($bundle|/private$bundle|/usr/lib/|/System/)" | sort -u || true)
fail=0
if [ -n "$outside" ]; then
	echo "loaded from outside the bundle:"; echo "$outside"; fail=1
fi
if ! grep -q 'window 0: .*visible=1' "$tmp/log"; then
	echo "no window:"; grep -v '^dyld\[' "$tmp/log" | head -20; fail=1
fi
if [ $fail = 0 ]; then
	echo "ok: $(grep -c '^dyld\[' "$tmp/log") images loaded, all from the bundle or the system; window up"
	if [ -n "${SHOT:-}" ]; then cp "$tmp/shot.png" "$SHOT"; fi
fi

# nexchat --tui: the bundled terminal UI, in a pty (script(1)), the same check
if [ -x "$tmp/nexchat.app/Contents/MacOS/nexchat-tui" ]; then
	env -i HOME="$HOME" TERM=xterm-256color DYLD_PRINT_LIBRARIES=1 \
		script -q "$tmp/tui.typescript" sh -c \
		"'$tmp/nexchat.app/Contents/MacOS/nexchat' --tui -d '$tmp/cfg' 2>'$tmp/tui.log' & p=\$!; sleep 3; kill \$p" \
		</dev/null >/dev/null 2>&1 || true
	tui_outside=$(grep -oE '^dyld\[[0-9]+\]: <[^>]*> /[^ ]+' "$tmp/tui.log" | awk '{print $3}' |
		grep -vE "^($bundle|/private$bundle|/usr/lib/|/System/)" | sort -u || true)
	if [ -n "$tui_outside" ]; then
		echo "nexchat --tui loaded from outside the bundle:"; echo "$tui_outside"; fail=1
	elif ! grep -q 'nexchat-tui' "$tmp/tui.log"; then
		echo "nexchat --tui didn't run the bundled nexchat-tui:"; head -5 "$tmp/tui.log"; fail=1
	else
		echo "ok: nexchat --tui runs the bundled nexchat-tui, $(grep -c '^dyld\[' "$tmp/tui.log") images, all from the bundle or the system"
	fi
fi
exit $fail

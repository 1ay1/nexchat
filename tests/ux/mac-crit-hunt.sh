#!/bin/sh
# Run nexchat under lldb with criticals as breakpoints while the real
# mouse sweeps the window (tooltips, hovers); print the first backtrace.
# usage: tests/ux/mac-crit-hunt.sh BIN [CFGSRC]
set -eu
bin=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
here=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
mkdir "$tmp/cfg"
src=${2:-}
if [ -n "$src" ]; then cp "$src/hexchat.conf" "$src/colors.conf" "$tmp/cfg/" 2>/dev/null || true; fi
printf 'gui_slist_skip = 1\ngui_win_left = 100\ngui_win_top = 100\ngui_win_width = 900\ngui_win_height = 600\ngui_win_save = 0\n' >> "$tmp/cfg/hexchat.conf"
python3 "$here/tests/fake_ircd.py" --demo --timeout 90 --log "$tmp/s.log" --portfile "$tmp/port" &
srv=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
cat > "$tmp/cmds" <<EOF
settings set target.env-vars NEXCHAT_GDK_NOISE=1 GTK_A11Y=none G_DEBUG=fatal-criticals
run -n -d $tmp/cfg irc://127.0.0.1:-$(cat "$tmp/port")/#nexchat,#help,#three
bt 40
kill
quit
EOF
( sleep 6; "$here/build-tools/macmouse" wiggle 120 140 860 540 25 2>/dev/null || /tmp/macmouse wiggle 120 140 860 540 25 ) &
mv=$!
timeout 60 lldb -s "$tmp/cmds" "$bin" > "$tmp/lldb.log" 2>&1 || true
kill $mv $srv 2>/dev/null || true
grep -nE "stop reason|frame #|CRITICAL|exited" "$tmp/lldb.log" | head -60
cp "$tmp/lldb.log" /tmp/crit-hunt.log
rm -rf "$tmp"

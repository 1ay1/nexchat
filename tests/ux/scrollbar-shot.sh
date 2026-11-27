#!/bin/sh
# The main window with a scrolled channel and the user list, shot by the app
# itself (NEXCHAT_SHOT: GTK renders the window, so no xwd and any backend).
# usage: tests/ux/scrollbar-shot.sh BIN OUT.png   env: SCHEME="Dracula" (colors.conf)
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1; out=$2
here=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
srv=; xv=
trap 'kill $srv $xv 2>/dev/null || true; rm -rf "$tmp"' EXIT
if [ -z "${NX_WAYLAND:-}" ]; then
	start_xvfb "${XVFB:-$(command -v Xvfb)}" "${DISP:-:66}"
	export DISPLAY=${DISP:-:66}
fi
python3 - "$tmp/script" <<'EOF'
import sys
l = [":u%d!u@h JOIN #s" % i for i in range(60)]
l += [":n%d!u@h PRIVMSG #s :line %d of a channel long enough to scroll" % (i % 9, i) for i in range(150)]
open(sys.argv[1], "w").write("\n".join(l) + "\n")
EOF
python3 "$here/tests/fake_ircd.py" --demo --timeout 30 --log "$tmp/s.log" --portfile "$tmp/port" --script "$tmp/script" >/dev/null 2>&1 &
srv=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
mkdir "$tmp/cfg"
printf 'gui_slist_skip = 1\nirc_nick1 = sb\ngui_win_width = 900\ngui_win_height = 560\n' > "$tmp/cfg/hexchat.conf"
[ -n "${SCHEME:-}" ] && printf 'scheme = %s\n' "$SCHEME" > "$tmp/cfg/colors.conf"
NEXCHAT_SHOT="$out:5" timeout 8 "$bin" -n -d "$tmp/cfg" \
	"irc://127.0.0.1:-$(cat "$tmp/port")/#s" >"$tmp/log" 2>&1 || true
[ -s "$out" ] || { echo "no shot"; tail -5 "$tmp/log"; exit 1; }

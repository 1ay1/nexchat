#!/bin/sh
# Run the Windows build under wine on a private Xvfb and screenshot it.
# usage: tests/win/wine-shot.sh NEXCHAT.EXE OUT.png [SECONDS] [extra conf lines]
#        CONNECT=1 joins #nexchat on tests/fake_ircd.py --demo
#        KEYS="..." / CLICKS: passed to tests/ux/xkey.py after startup
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
exe=$1; out=$2; wait_s=${3:-25}; extra=${4:-}
s=${SYSROOT:-$HOME/.cache/nexchat-win/ucrt64}
export WINEPREFIX=${WINEPREFIX:-$HOME/.cache/nexchat-win/wineprefix}
export WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
xvfb=${XVFB:-$(command -v Xvfb)}
xwd=${XWD:-$(command -v xwd)}
disp=${DISP:-:66}
tmp=$(mktemp -d)
xv=; ircd=
trap 'wineserver -k 2>/dev/null || true; kill $xv $ircd 2>/dev/null || true; rm -rf "$tmp"' EXIT

"$xvfb" "$disp" -screen 0 ${SIZE:-1280x800}x24 -nolisten tcp >/dev/null 2>&1 &
xv=$!
DISPLAY=$disp python3 "$here/ux/xkey.py" --wait-display
mkdir -p "$tmp/cfg"
printf 'gui_slist_skip = 1\nirc_nick1 = win\ngui_win_left = 40\ngui_win_top = 40\nnet_throttle = 0\n%s\n' "$extra" > "$tmp/cfg/hexchat.conf"
url=
if [ -n "${CONNECT:-}" ]; then
	python3 "$here/fake_ircd.py" --demo --timeout 90 --log "$tmp/srv.log" --portfile "$tmp/port" 2>/dev/null &
	ircd=$!
	i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
	url="irc://127.0.0.1:-$(cat "$tmp/port")/#nexchat"
fi
# the GTK/glib DLLs, schemas, icons and pixbuf loaders from the sysroot.
# PANGOCAIRO_BACKEND=fc: wine's DirectWrite hints small glyphs into
# garbage; real Windows doesn't, this only makes the shots readable
DISPLAY=$disp WINEPATH="Z:$s/bin" XDG_DATA_DIRS="$s/share" GSK_RENDERER=${GSK_RENDERER:-} \
	PANGOCAIRO_BACKEND=${PANGOCAIRO_BACKEND:-fc} NX_DUMP_CSS=${NX_DUMP_CSS:-} \
	NEXCHAT_MENU_ACTIVATE=${NEXCHAT_MENU_ACTIVATE:-} \
	GDK_SCALE=${GDK_SCALE:-} GDK_DPI_SCALE=${GDK_DPI_SCALE:-} \
	wine "$exe" -a -d "Z:$tmp/cfg" $url >"$tmp/log" 2>&1 &
sleep "$wait_s"
if [ -n "${KEYS:-}" ]; then
	# shellcheck disable=SC2086
	DISPLAY=$disp python3 "$here/ux/xkey.py" --delay 0.5 --at 400,300 $KEYS
	sleep 2
fi
DISPLAY=$disp "$xwd" -root -silent -out "$tmp/s.xwd"
python3 "$here/ux/xwd2png.py" "$tmp/s.xwd" "$out"
[ -n "${KEEPLOG:-}" ] && cp "$tmp/log" "$KEEPLOG"
grep -iE "critical|error|warning" "$tmp/log" | grep -viE "mesa|dri|pci id|egl" | sort | uniq -c | sort -rn | head -15 || true

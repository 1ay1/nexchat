#!/bin/sh
# The GTK4 tray (StatusNotifierItem + dbusmenu): on a private session bus a
# fake panel (tests/sni-panel.py) is the StatusNotifierWatcher; nexchat
# registers with it, the panel reads the icon and the menu, clicks
# "Blink on > Channel Message" and reads the menu again. The menu must
# match the GtkStatusIcon one (tray.expected, same items, mnemonics and
# states). Skipped (77) on GTK3 builds, without dbus-run-session or
# python3-gi.
# usage: tests/sni.sh BIN     (needs Xvfb)
set -eu
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
command -v dbus-run-session >/dev/null || { echo "sni: no dbus-run-session, skipped"; exit 77; }
python3 -c 'from gi.repository import Gio' 2>/dev/null || { echo "sni: no python3-gi, skipped"; exit 77; }
if [ -z "${NX_IN_BUS:-}" ]; then
	NX_IN_BUS=1 exec dbus-run-session -- sh "$0" "$@"
fi
NX_UX_DIR=$here/ux
. "$here/ux/isolate.sh"
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:72}
tmp=$(mktemp -d)
app=; panel=; xv=
cleanup() { kill $app $panel $xv 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT
start_xvfb "$xvfb" "$disp"
mkdir -p "$tmp/cfg"
printf 'gui_slist_skip = 1\ninput_tray_priv = 1\ninput_tray_chans = 0\ngui_tray = 1\ngui_tray_blink = 1\n' > "$tmp/cfg/hexchat.conf"

python3 "$here/sni-panel.py" "$tmp/menu" >"$tmp/panel.log" 2>&1 &
panel=$!
# the watcher name is up before nexchat looks for it
wait_for gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
	--method org.freedesktop.DBus.GetNameOwner org.kde.StatusNotifierWatcher
DISPLAY=$disp "$bin" -a -d "$tmp/cfg" >"$tmp/log" 2>&1 &
app=$!
if ! wait $panel; then
	panel=
	# a GTK3 build has no StatusNotifierItem: nothing registers
	grep -q "no item registered" "$tmp/panel.log" && { echo "sni: no item (GTK3 build?), skipped"; exit 77; }
	echo "sni: FAIL, panel:"; cat "$tmp/panel.log"; exit 1
fi
panel=

fail=0
grep -q '^icon: 1 pixmap' "$tmp/menu" && echo "ok:   icon exported" || { echo "FAIL: no icon"; fail=1; }
grep -q '^blink: yes' "$tmp/menu" && echo "ok:   icon blinks (NewIcon)" || { echo "FAIL: $(grep '^blink' "$tmp/menu")"; fail=1; }
grep -q '^tooltip: hellotray$' "$tmp/menu" && echo "ok:   tooltip" || { echo "FAIL: $(grep '^tooltip' "$tmp/menu")"; fail=1; }
# the menu is the GtkStatusIcon baseline, with a panel click instead of
# /set between the two halves
grep -v '^icon: \|^blink: \|^tooltip: ' "$tmp/menu" | sed 's/^## after clicking Channel Message$/## after input_tray_chans 1/' > "$tmp/actual"
if diff -u "$here/ux/tray.expected" "$tmp/actual"; then echo "ok:   menu matches tray.expected"
else echo "FAIL: menu (diff above)"; fail=1; fi
kill -0 "$app" 2>/dev/null || { echo "FAIL: nexchat died"; tail "$tmp/log"; fail=1; }
if grep -q CRITICAL "$tmp/log"; then echo "FAIL: criticals"; grep CRITICAL "$tmp/log" | head; fail=1; fi
[ $fail -eq 0 ] && echo "sni: ok" || exit 1

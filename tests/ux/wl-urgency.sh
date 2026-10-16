#!/bin/sh
# Taskbar flashing on Wayland: /GUI FLASH while another window has focus
# must mark our window urgent in the compositor, and focusing it again
# clears that. Runs under tests/ux/with-sway.sh (sway shows urgency on IPC).
# usage: with-sway.sh sh tests/ux/wl-urgency.sh BIN
set -eu
bin=$1
command -v swaymsg >/dev/null || { echo "wl-urgency: no swaymsg, skipped"; exit 77; }
command -v python3 >/dev/null || { echo "wl-urgency: no python3, skipped"; exit 77; }
tmp=$(mktemp -d)
app=; thief=
cleanup() { kill $app $thief 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT
fail=0
ok() { echo "ok:   $1"; }
bad() { echo "FAIL: $1"; fail=1; }

# urgent flag of the window whose app_id/title matches $1
urgent() {
	swaymsg -t get_tree | python3 -c '
import json, sys
want = sys.argv[1]
def walk(n):
    if (n.get("app_id") or "") == want or want in (n.get("name") or ""):
        print("yes" if n.get("urgent") else "no"); sys.exit()
    for c in n.get("nodes", []) + n.get("floating_nodes", []):
        walk(c)
walk(json.load(sys.stdin)); print("none")' "$1"
}

mkdir -p "$tmp/cfg"
printf 'gui_slist_skip = 1\ngui_tray = 0\n' > "$tmp/cfg/hexchat.conf"
"$bin" -a -d "$tmp/cfg" >"$tmp/log" 2>&1 &
app=$!
i=0; while [ "$(urgent nexchat)" = none ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
[ "$(urgent nexchat)" = none ] && { echo "FAIL: no nexchat window"; swaymsg -t get_tree | head -50; cat "$tmp/log"; exit 1; }
[ "$(urgent nexchat)" = no ] && ok "not urgent at start" || bad "urgent at start"

# a second toplevel takes focus
python3 -c '
import gi
try:
    gi.require_version("Gtk", "3.0")
except ValueError:
    gi.require_version("Gtk", "4.0")
from gi.repository import Gtk, GLib
w = Gtk.Window(title="nx-focus-thief")
w.present()
GLib.MainLoop().run()' >"$tmp/thief.log" 2>&1 &
thief=$!
i=0; while [ "$(urgent nx-focus-thief)" = none ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
[ "$(urgent nx-focus-thief)" = none ] && { echo "wl-urgency: no focus thief window, skipped"; cat "$tmp/thief.log"; exit 77; }
swaymsg '[title="nx-focus-thief"] focus' >/dev/null
sleep 0.5
focused=$(swaymsg -t get_tree | python3 -c '
import json, sys
def walk(n):
    if n.get("focused"): print(n.get("app_id") or n.get("name")); sys.exit()
    for c in n.get("nodes", []) + n.get("floating_nodes", []): walk(c)
walk(json.load(sys.stdin))')
case $focused in nexchat*) bad "focus thief didn't take focus ($focused)";; esac

"$bin" -d "$tmp/cfg" -e -c "gui flash" >/dev/null 2>&1 || true
i=0; while [ "$(urgent nexchat)" = no ] && [ $i -lt 30 ]; do sleep 0.1; i=$((i+1)); done
[ "$(urgent nexchat)" = yes ] && ok "/gui flash marks the window urgent" || bad "not urgent after /gui flash"

swaymsg '[app_id="nexchat"] focus' >/dev/null
i=0; while [ "$(urgent nexchat)" = yes ] && [ $i -lt 30 ]; do sleep 0.1; i=$((i+1)); done
[ "$(urgent nexchat)" = no ] && ok "focusing it clears urgency" || bad "still urgent after focus"

if grep -q CRITICAL "$tmp/log"; then echo "FAIL: criticals"; grep CRITICAL "$tmp/log" | head; fail=1; fi
[ $fail -eq 0 ] && echo "wl-urgency: ok" || exit 1

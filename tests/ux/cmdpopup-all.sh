#!/bin/sh
# The full "/" list: every command once, every group heading once, in
# order (Most used first, then the groups, then your commands, plugins,
# the rest).
# usage: tests/ux/cmdpopup-all.sh path/to/nexchat     (needs Xvfb + XTest)
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:96}
tmp=$(mktemp -d)
app=; ircd=; xv=
trap 'kill $app $ircd $xv 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"' EXIT

start_xvfb "$xvfb" "$disp"
mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = smoke\ngui_slist_skip = 1\ngui_win_left = 0\ngui_win_top = 0\ngui_win_width = 900\ngui_win_height = 600\n' > "$tmp/cfg/hexchat.conf"
python3 "$here/../fake_ircd.py" --timeout 60 --log "$tmp/server.log" --portfile "$tmp/port" 2>/dev/null &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
DISPLAY=$disp NX_DESCRIBE_ALL=1 NEXCHAT_TABS_DUMP=1 \
	"$bin" -a -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/#t" >"$tmp/log" 2>&1 &
app=$!
wait_window "$disp"
wait_for grep -q '^tab: #t' "$tmp/log"
sleep 0.5
DISPLAY=$disp python3 "$here/xkey.py" --delay 0.3 --at 450,580 --type "/"
sleep 2.5
awk '/^tabs:$/{n++} {r[n]=r[n] $0 "\n"} END{printf "%s", r[n-1]}' "$tmp/log" | grep '^cmdpopup' > "$tmp/list" || true
python3 - "$tmp/list" <<'EOF'
import collections, re, sys
s = open(sys.argv[1]).read().split(" | ")[0]
heads = re.findall(r"\{([^}]*)\}", s)
names = [w.strip("[]") for w in re.sub(r"\{[^}]*\}", "", s).split()[2:] if not w.startswith("sel=")]
fail = 0
def check(ok, what):
    global fail
    print(("ok:   " if ok else "FAIL: ") + what)
    fail |= not ok
dh = [h for h, c in collections.Counter(heads).items() if c > 1]
dn = [n for n, c in collections.Counter(names).items() if c > 1]
check(len(names) > 80, f"lists every command ({len(names)})")
check(not dn, f"no command twice {dn}")
check(not dh, f"no group heading twice {dh}")
check(heads[:1] == ["Most used"], "Most used first")
check("Channels" in heads and "Talking" in heads, "groups shown")
sys.exit(fail)
EOF
echo "cmdpopup-all: ok"

#!/bin/sh
# /LASTLOG -a: search every open channel and query. The (lastlog) window
# gets each window's hits under its name; a window without hits isn't
# listed.
# usage: tests/ux/lastlog-all.sh path/to/nexchat     (needs Xvfb + XTest)
set -eu
. "$(dirname "$0")/isolate.sh"
bin=$1
here=$(cd "$(dirname "$0")" && pwd)
xvfb=${XVFB:-$(command -v Xvfb || echo /opt/X11/bin/Xvfb)}
disp=${DISP:-:90}
tmp=$(mktemp -d)
app=; ircd=; xv=
trap 'kill $app $ircd $xv 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"' EXIT

cat > "$tmp/script" <<'EOF'
:bob!b@h PRIVMSG #alpha :the needle is here
:bob!b@h PRIVMSG #alpha :nothing
:carol!c@h PRIVMSG #bravo :another NEEDLE, upper case
:dave!d@h PRIVMSG #charlie :no match in this one
EOF
start_xvfb "$xvfb" "$disp"
mkdir -p "$tmp/cfg"
printf 'net_throttle = 0\nirc_nick1 = smoke\ngui_slist_skip = 1\ngui_win_left = 0\ngui_win_top = 0\ngui_win_width = 900\ngui_win_height = 600\n' > "$tmp/cfg/hexchat.conf"
python3 "$here/../fake_ircd.py" --timeout 60 --log "$tmp/server.log" --portfile "$tmp/port" \
	--script "$tmp/script" --script-after '#charlie' 2>/dev/null &
ircd=$!
i=0; while [ ! -s "$tmp/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done
DISPLAY=$disp NEXCHAT_TABS_DUMP=text \
	"$bin" -a -d "$tmp/cfg" "irc://127.0.0.1:-$(cat "$tmp/port")/#alpha,#bravo,#charlie" >"$tmp/log" 2>&1 &
app=$!
wait_window "$disp"
wait_for grep -q '^tab: #charlie' "$tmp/log"
sleep 1

DISPLAY=$disp python3 "$here/xkey.py" --delay 0.4 --at 450,580 --type "/lastlog -a needle" Return
sleep 2.5

# the last dump round, while (lastlog) is in front
last() { awk '/^tabs:$/{n++} {r[n]=r[n] $0 "\n"} END{printf "%s", r[n-1]}' "$tmp/log"; }
fail=0
has() { if last | grep -q -- "$1"; then echo "ok:   $2"; else echo "FAIL: $2"; fail=1; fi; }
hasnt() { if last | grep -q -- "$1"; then echo "FAIL: $2"; fail=1; else echo "ok:   $2"; fi; }
has 'tab: (lastlog)' 'a (lastlog) window opens'
has '^line: .*#alpha.*: 1' '#alpha is listed with 1 hit'
has '^line: .*the needle is here' '#alpha hit shown'
has '^line: .*#bravo.*: 1' '#bravo listed (case-insensitive by default)'
has '^line: .*another NEEDLE' '#bravo hit shown'
hasnt '^line: .*#charlie' '#charlie, without hits, is not listed'
hasnt '^line: .*no match in this one' 'non-matching lines left out'
if [ $fail -ne 0 ]; then last | grep '^line:'; exit 1; fi
echo "lastlog-all: ok"

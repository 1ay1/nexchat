#!/bin/sh
# /LASTLOG -l: search the log files on disk, every network and channel,
# matches grouped under each file's name.
# usage: tests/lastlog-logs.sh path/to/nexchat-text
set -eu
bin=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/cfg/logs/Libera.Chat" "$tmp/cfg/logs/OFTC"
printf 'irc_nick1 = t\n' > "$tmp/cfg/hexchat.conf"
cat > "$tmp/cfg/logs/Libera.Chat/#linux.log" <<'EOF'
Jan 01 10:00:00 <alice>	the kernel release is out
Jan 01 10:01:00 <bob>	nothing to see
EOF
cat > "$tmp/cfg/logs/OFTC/#debian.log" <<'EOF'
Feb 02 11:00:00 <carol>	new Release candidate today
EOF
printf 'Mar 03 12:00:00 <dave>\tunrelated\n' > "$tmp/cfg/logs/OFTC/#misc.log"
{
	sleep 1; echo "/lastlog -l release"; sleep 1; echo "/quit"
} | timeout 15 "$bin" -n -d "$tmp/cfg" > "$tmp/out" 2>&1 || true

fail=0
want() { grep -q -- "$1" "$tmp/out" && echo "ok:   $2" || { echo "FAIL: $2"; fail=1; }; }
none() { if grep -q -- "$1" "$tmp/out"; then echo "FAIL: $2"; fail=1; else echo "ok:   $2"; fi; }
# the file names come with the platform's separator (\ on Windows)
want 'Libera.Chat[/\]#linux.log' 'a match in one network: listed under its file'
want 'the kernel release is out' 'the matching line'
want 'OFTC[/\]#debian.log' 'another network too'
want 'Release candidate' 'case-insensitive by default'
none 'nothing to see' 'lines without a match left out'
none '#misc.log' 'files without a match left out'
[ $fail -eq 0 ] || { cat "$tmp/out"; exit 1; }
echo "lastlog-logs: ok"

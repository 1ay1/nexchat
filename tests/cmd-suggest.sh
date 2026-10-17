#!/bin/sh
# An unknown command (not connected, so it isn't passed to the server)
# suggests the nearest real one: /jion -> /JOIN; nothing for gibberish.
# usage: tests/cmd-suggest.sh path/to/nexchat-text
set -eu
bin=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/cfg"
printf 'irc_nick1 = t\n' > "$tmp/cfg/hexchat.conf"
{
	sleep 1; echo "/jion #x"; echo "/serer foo"; echo "/zzzzqq"; sleep 1; echo "/quit"
} | timeout 15 "$bin" -n -d "$tmp/cfg" > "$tmp/out" 2>&1 || true
fail=0
grep -q "Unknown Command jion. Did you mean /JOIN?" "$tmp/out" && echo "ok:   /jion -> /JOIN" || { echo "FAIL: /jion"; fail=1; }
grep -q "Unknown Command serer. Did you mean /SERVER?" "$tmp/out" && echo "ok:   /serer -> /SERVER" || { echo "FAIL: /serer"; fail=1; }
grep -q "Unknown Command zzzzqq. Try /help" "$tmp/out" && echo "ok:   gibberish: no guess" || { echo "FAIL: /zzzzqq"; fail=1; }
[ $fail -eq 0 ] || { cat "$tmp/out"; exit 1; }
echo "cmd-suggest: ok"

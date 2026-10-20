#!/bin/sh
# nexchat-tui in a pty, keys and mouse (pyte as the terminal).
# usage: tests/tui-smoke.sh path/to/nexchat-tui [script.py]   (default tui-smoke.py)
set -u
here=$(cd "$(dirname "$0")" && pwd)
# meson's PATH may find a python3 without pyte: try every python3 there is
# (PATH first, then Homebrew's and the system's) for one that has it
try () {
	for py in "$@"; do
		[ -x "$py" ] || continue
		case $py in *-config) continue ;; esac
		if "$py" -c 'import pyte' 2>/dev/null; then
			exec "$py" "$here/${script:-tui-smoke.py}" "$bin"
		fi
	done
}
bin=$1
script=${2:-tui-smoke.py}
old_ifs=$IFS
IFS=:
for d in $PATH; do
	IFS=$old_ifs
	try "$d"/python3 "$d"/python3.[0-9]*
done
IFS=$old_ifs
try /opt/homebrew/opt/python@3*/libexec/bin/python3 /opt/homebrew/bin/python3* \
    /usr/local/bin/python3* /usr/bin/python3
echo "tui test: skipped (needs: python3 -m pip install pyte)"
exit 77

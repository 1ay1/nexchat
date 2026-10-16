#!/bin/sh
# First run with only a HexChat config dir: it must be copied (not moved) to
# the nexchat dir, and a second run must not touch either.
# usage: tests/migrate-config.sh path/to/nexchat-text
set -eu
bin=$1
# Windows migrates %APPDATA%\HexChat -> %APPDATA%\nexchat (get_old_xdir),
# which XDG_CONFIG_HOME can't redirect, and MSYS can't make the symlink
# fixture. 77 = skipped for meson.
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) echo "migrate-config: skipped on Windows"; exit 77;; esac
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

export XDG_CONFIG_HOME="$tmp/config"
old="$XDG_CONFIG_HOME/hexchat"
new="$XDG_CONFIG_HOME/nexchat"
mkdir -p "$old/addons" "$old/logs/net"
printf 'irc_nick1 = migrated_nick\n' > "$old/hexchat.conf"
printf 'N=MyNet\nS=irc.example.org\n\n' > "$old/servlist.conf"
printf 'print("hi")\n' > "$old/addons/script.py"
printf 'log line\n' > "$old/logs/net/#chan.log"
ln -s /etc/hostname "$old/addons/link"
before=$(cd "$old" && find . | sort | xargs -I{} sh -c 'printf "%s " "{}"; [ -f "{}" ] && cksum < "{}" || echo' )

# -u prints the config dir and exits, after get_xdir() has run
"$bin" -u > "$tmp/out" 2>&1 || true

fail=0
[ -d "$new" ] || { echo "FAIL: $new not created"; fail=1; }
for f in hexchat.conf servlist.conf addons/script.py "logs/net/#chan.log"; do
	cmp -s "$old/$f" "$new/$f" || { echo "FAIL: $f not copied"; fail=1; }
done
[ -e "$new/addons/link" ] && { echo "FAIL: symlink was copied"; fail=1; }
after=$(cd "$old" && find . | sort | xargs -I{} sh -c 'printf "%s " "{}"; [ -f "{}" ] && cksum < "{}" || echo' )
[ "$before" = "$after" ] || { echo "FAIL: old dir changed"; fail=1; }
grep -q "$new" "$tmp/out" || { echo "FAIL: -u did not report $new"; cat "$tmp/out"; fail=1; }

# second run: user changed the nexchat copy, it must not be overwritten
printf 'irc_nick1 = changed\n' > "$new/hexchat.conf"
"$bin" -u > /dev/null 2>&1 || true
grep -q changed "$new/hexchat.conf" || { echo "FAIL: second run overwrote nexchat config"; fail=1; }

# -d must not trigger any migration
rm -rf "$new"
"$bin" -d "$tmp/custom" -u > /dev/null 2>&1 || true
[ -e "$new" ] && { echo "FAIL: -d still migrated into $new"; fail=1; }

[ $fail -eq 0 ] && echo "migrate-config: ok"
exit $fail

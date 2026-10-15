#!/bin/sh
# Load one script per language that uses the new `nexchat` name AND one that
# uses the old `hexchat` name, and check they all print their marker.
# usage: tests/plugin-aliases.sh <install prefix>
set -eu
prefix=$1
bin="$prefix/bin/nexchat-text"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cfg="$tmp/cfg"
mkdir -p "$cfg/addons"

cat > "$cfg/addons/new.py" <<'EOF'
import nexchat
__module_name__ = "new_py"; __module_version__ = "1"; __module_description__ = ""
nexchat.prnt("MARK-py-nexchat " + nexchat.get_info("version"))
EOF
cat > "$cfg/addons/old.py" <<'EOF'
import hexchat
__module_name__ = "old_py"; __module_version__ = "1"; __module_description__ = ""
hexchat.prnt("MARK-py-hexchat")
EOF
cat > "$cfg/addons/new.lua" <<'EOF'
nexchat.register("new_lua", "1", "")
nexchat.print("MARK-lua-nexchat")
EOF
cat > "$cfg/addons/old.lua" <<'EOF'
hexchat.register("old_lua", "1", "")
hexchat.print("MARK-lua-hexchat")
EOF
cat > "$cfg/addons/new.pl" <<'EOF'
Nexchat::register("new_pl", "1", "");
Nexchat::print("MARK-pl-nexchat");
EOF
cat > "$cfg/addons/old.pl" <<'EOF'
HexChat::register("old_pl", "1", "");
HexChat::print("MARK-pl-hexchat");
EOF

{ sleep 4; echo "/quit"; sleep 1; } | timeout 15 "$bin" -a -d "$cfg" > "$tmp/out" 2>&1 || true

fail=0
for m in py-nexchat py-hexchat lua-nexchat lua-hexchat pl-nexchat pl-hexchat; do
	grep -q "MARK-$m" "$tmp/out" || { echo "FAIL: MARK-$m missing"; fail=1; }
done
[ $fail -eq 0 ] && echo "plugin-aliases: ok" || tail -30 "$tmp/out"
exit $fail

#!/bin/sh
# Windows release packages, from an MSYS2 UCRT64 shell after building:
#   meson setup build --prefix=/ucrt64 ... && ninja -C build
#   sh win32/package.sh build 0.1.0
# makes, in dist/:
#   nexchat-0.1.0-windows-x64-portable.zip   unzip anywhere and run; settings
#                                             stay in its config\ folder
#   nexchat-0.1.0-windows-x64-setup.exe      installer (Start menu, uninstall,
#                                             irc:// links), if Inno Setup's
#                                             iscc is on PATH
# Both carry nexchat.exe, the DLLs it loads (found by ntldd, so nothing
# extra), GTK's runtime data and nexchat's translations and plugins.
set -eu
build=$1
version=$2
here=$(cd "$(dirname "$0")/.." && pwd)
stage=$here/dist/stage/nexchat
prefix=${MINGW_PREFIX:-/ucrt64}
rm -rf "$here/dist/stage"
mkdir -p "$stage"

# nexchat itself, laid out as installed (bin/, share/, lib/)
DESTDIR="$here/dist/stage/root" meson install -C "$build" --no-rebuild >/dev/null
root=$here/dist/stage/root$prefix
cp -R "$root"/. "$stage"/
rm -rf "$here/dist/stage/root"

# every DLL the program and its plugins load, from the MSYS2 prefix
deps () {
	ntldd -R "$@" 2>/dev/null | sed -n 's/.*=> \(.*\.dll\).*/\1/ip' |
		tr '\\' '/' | grep -i "$(cygpath -m "$prefix")" | sort -u
}
for f in $(deps "$stage"/bin/*.exe "$stage"/lib/nexchat/plugins/*.dll); do
	cp -n "$(cygpath -u "$f")" "$stage/bin/" 2>/dev/null || true
done
# GTK loads these at run time, not by linking: the image loaders (SVG
# icons) and GIO's modules
cp -n "$prefix"/bin/librsvg-2-2.dll "$stage/bin/" 2>/dev/null || true
for f in $(deps "$prefix"/bin/librsvg-2-2.dll); do cp -n "$(cygpath -u "$f")" "$stage/bin/" 2>/dev/null || true; done
mkdir -p "$stage/lib/gdk-pixbuf-2.0"
cp -R "$prefix/lib/gdk-pixbuf-2.0/2.10.0" "$stage/lib/gdk-pixbuf-2.0/"
# the loader cache with paths relative to the bundle
( cd "$stage" && GDK_PIXBUF_MODULEDIR=lib/gdk-pixbuf-2.0/2.10.0/loaders \
	"$prefix/bin/gdk-pixbuf-query-loaders.exe" > lib/gdk-pixbuf-2.0/2.10.0/loaders.cache )

# GTK's settings schemas, icon theme and the bits of share/ it reads
mkdir -p "$stage/share/glib-2.0/schemas" "$stage/share/icons"
cp "$prefix"/share/glib-2.0/schemas/org.gtk.*.xml "$stage/share/glib-2.0/schemas/"
glib-compile-schemas "$stage/share/glib-2.0/schemas"
cp -R "$prefix/share/icons/Adwaita" "$stage/share/icons/" 2>/dev/null || true
cp -R "$prefix/share/icons/hicolor" "$stage/share/icons/" 2>/dev/null || true
gtk4-update-icon-cache -q -t -f "$stage/share/icons/Adwaita" 2>/dev/null || true
# GTK's own translations, next to nexchat's
for d in "$prefix"/share/locale/*/LC_MESSAGES; do
	l=$(basename "$(dirname "$d")")
	for m in gtk40 glib20; do
		[ -f "$d/$m.mo" ] && mkdir -p "$stage/share/locale/$l/LC_MESSAGES" &&
			cp "$d/$m.mo" "$stage/share/locale/$l/LC_MESSAGES/"
	done
done

# licence and readme
mkdir -p "$stage/share/doc/nexchat"
cp "$here/COPYING" "$stage/share/doc/nexchat/COPYING.txt"
cp "$here/readme.md" "$stage/share/doc/nexchat/readme.md"

# spellcheck: language and country names (the repo carries a copy), and
# enchant's providers
mkdir -p "$stage/share/xml/iso-codes"
cp "$here"/data/iso-codes/iso_639.xml "$here"/data/iso-codes/iso_3166.xml "$stage/share/xml/iso-codes/"
if [ -d "$prefix/lib/enchant-2" ]; then
	mkdir -p "$stage/lib/enchant-2"
	cp "$prefix"/lib/enchant-2/*.dll "$stage/lib/enchant-2/" 2>/dev/null || true
	for f in $(deps "$stage"/lib/enchant-2/*.dll); do cp -n "$(cygpath -u "$f")" "$stage/bin/" 2>/dev/null || true; done
fi

# nothing a user runs needs these
rm -rf "$stage/include" "$stage/lib/pkgconfig" "$stage/share/man" "$stage/share/applications" \
	"$stage/share/metainfo" "$stage/share/dbus-1"
find "$stage" -name '*.a' -delete

# ---- portable zip: the same folder plus the marker that keeps settings
# beside the program instead of in AppData ----
mkdir -p "$here/dist"
portable=$here/dist/nexchat-$version-windows-x64-portable
rm -rf "$portable" "$portable.zip"
cp -R "$stage" "$portable"
: > "$portable/portable-mode"	# next to bin\: the "installation directory" GLib reports
# a top-level launcher so it's obvious what to run
cat > "$portable/nexchat.bat" <<'EOF'
@start "" "%~dp0bin\nexchat.exe" %*
EOF
( cd "$here/dist" && 7z a -tzip -mx=9 "$(basename "$portable").zip" "$(basename "$portable")" >/dev/null )
rm -rf "$portable"
echo "made dist/$(basename "$portable").zip"

# ---- installer ----
if command -v iscc >/dev/null 2>&1; then
	iscc -Q -DAPPVER="$version" -DSTAGE="$(cygpath -w "$stage")" \
		-DOUTDIR="$(cygpath -w "$here/dist")" -DSRCDIR="$(cygpath -w "$here")" \
		"$(cygpath -w "$here/win32/installer/nexchat.iss")"
	echo "made dist/nexchat-$version-windows-x64-setup.exe"
else
	echo "iscc (Inno Setup) not found: no installer made" >&2
fi

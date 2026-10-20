#!/bin/sh
# macOS release disk image from a built bundle:
#   ninja -C build app && sh osx/make-dmg.sh build/nexchat.app 0.1.0
# makes dist/nexchat-0.1.0-macos-<arch>.dmg: nexchat.app and an
# Applications link to drag it onto.
set -eu
app=$1
version=$2
here=$(cd "$(dirname "$0")/.." && pwd)
arch=$(lipo -archs "$app/Contents/MacOS/nexchat" 2>/dev/null | tr ' ' '-' || uname -m)
out=$here/dist/nexchat-$version-macos-$arch.dmg
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

mkdir -p "$here/dist"
cp -R "$app" "$stage/nexchat.app"
ln -s /Applications "$stage/Applications"
rm -f "$out"
hdiutil create -quiet -volname "nexchat $version" -srcfolder "$stage" \
	-fs HFS+ -format UDZO -imagekey zlib-level=9 -ov "$out"
echo "made dist/$(basename "$out")"

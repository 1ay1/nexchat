#!/bin/sh
# macOS release disk image from a built bundle:
#   ninja -C build app && sh osx/make-dmg.sh build/nexchat.app 0.1.0
# makes dist/nexchat-0.1.0-macos-<arch>.dmg: nexchat.app and an
# Applications link to drag it onto.
#
# CODESIGN_IDENTITY empty or "-": the DMG is not signed (ad hoc app, as before).
# A Developer ID Application name: the DMG is signed with a secure timestamp.
# NOTARY_KEY_PATH, NOTARY_KEY_ID and NOTARY_ISSUER_ID as well: the app is
# notarized and stapled, then the DMG is signed, submitted with notarytool
# (App Store Connect API key), stapled, and checked. See osx/SIGNING.md.
set -eu
app=$1
version=$2
here=$(cd "$(dirname "$0")/.." && pwd)
arch=$(lipo -archs "$app/Contents/MacOS/nexchat" 2>/dev/null | tr ' ' '-' || uname -m)
out=$here/dist/nexchat-$version-macos-$arch.dmg
stage=$(mktemp -d)
mnt=""
zdir=""
cleanup() {
	rm -rf "$stage"
	if [ -n "$mnt" ]; then
		hdiutil detach "$mnt" >/dev/null 2>&1 || true
		rmdir "$mnt" 2>/dev/null || true
	fi
	if [ -n "$zdir" ]; then
		rm -rf "$zdir"
	fi
}
trap cleanup EXIT

notary_field() {
	# $1 json file, $2 field name. Empty if the file isn't notarytool's JSON.
	python3 -c '
import json, sys
text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
start, end = text.find("{"), text.rfind("}")
if start < 0 or end < start:
    sys.exit(0)
try:
    obj = json.loads(text[start:end + 1])
except ValueError:
    sys.exit(0)
sys.stdout.write(str(obj.get(sys.argv[2]) or ""))
' "$1" "$2"
}

notary_log() {
	id=$(notary_field "$1" id)
	if [ -n "$id" ]; then
		echo "Notary log for $id:"
		xcrun notarytool log "$id" \
			--key "$NOTARY_KEY_PATH" \
			--key-id "$NOTARY_KEY_ID" \
			--issuer "$NOTARY_ISSUER_ID" || true
	fi
}

# Submit $1 and wait. Exit non-zero prints Apple's rejection log.
notary_submit() {
	echo "Submitting $(basename "$1") to the Apple notary service"
	log=$(mktemp)
	if ! xcrun notarytool submit "$1" \
		--key "$NOTARY_KEY_PATH" \
		--key-id "$NOTARY_KEY_ID" \
		--issuer "$NOTARY_ISSUER_ID" \
		--wait --timeout 30m \
		--output-format json >"$log"; then
		echo "notarytool submit failed for $(basename "$1")"
		cat "$log"
		notary_log "$log"
		rm -f "$log"
		exit 1
	fi
	cat "$log"
	status=$(notary_field "$log" status)
	if [ "$status" != "Accepted" ]; then
		echo "notarization status for $(basename "$1"): ${status:-unknown}"
		notary_log "$log"
		rm -f "$log"
		exit 1
	fi
	rm -f "$log"
}

notary_count=0
[ -n "${NOTARY_KEY_PATH:-}" ] && notary_count=$((notary_count + 1))
[ -n "${NOTARY_KEY_ID:-}" ] && notary_count=$((notary_count + 1))
[ -n "${NOTARY_ISSUER_ID:-}" ] && notary_count=$((notary_count + 1))
if [ "$notary_count" -ne 0 ] && [ "$notary_count" -ne 3 ]; then
	echo "Need all of NOTARY_KEY_PATH, NOTARY_KEY_ID and NOTARY_ISSUER_ID."
	exit 1
fi

identity=${CODESIGN_IDENTITY:-}
case $identity in
""|-)
	if [ "$notary_count" -ne 0 ]; then
		echo "Notary credentials are set but CODESIGN_IDENTITY is missing."
		echo "Refusing to submit an ad hoc DMG."
		exit 1
	fi
	;;
esac

# Staple the app before the image is built, so a copy dragged to
# Applications still has a ticket when the Mac is offline. The DMG is
# notarized again below; Gatekeeper checks the image on open.
if [ "$notary_count" -eq 3 ]; then
	auth=$(codesign -dv --verbose=4 "$app" 2>&1 || true)
	echo "$auth" | grep -q "Authority=Developer ID Application" || {
		echo "Not submitting: $app is not signed by a Developer ID Application certificate."
		echo "Sign it first: python3 osx/make-app.py <builddir> --sign \"$identity\""
		exit 1
	}
	echo "$auth" | grep -q "(runtime)" || {
		echo "Not submitting: $app does not have the hardened runtime."
		exit 1
	}
	zdir=$(mktemp -d)
	# ditto keeps the bundle's signature; zip(1) does not.
	ditto -c -k --keepParent "$app" "$zdir/nexchat.zip"
	notary_submit "$zdir/nexchat.zip"
	rm -rf "$zdir"
	zdir=""
	xcrun stapler staple "$app"
	xcrun stapler validate "$app"
	codesign --verify --deep --strict --verbose=2 "$app"
fi

mkdir -p "$here/dist"
cp -R "$app" "$stage/nexchat.app"
ln -s /Applications "$stage/Applications"
rm -f "$out"
hdiutil create -quiet -volname "nexchat $version" -srcfolder "$stage" \
	-fs HFS+ -format UDZO -imagekey zlib-level=9 -ov "$out"

case $identity in
""|-)
	echo "made dist/$(basename "$out") (ad hoc: no Developer ID, not notarized)"
	exit 0
	;;
esac

echo "Signing DMG with $identity"
if [ -n "${CODESIGN_KEYCHAIN:-}" ]; then
	codesign --force --timestamp --sign "$identity" --keychain "$CODESIGN_KEYCHAIN" "$out"
else
	codesign --force --timestamp --sign "$identity" "$out"
fi

# The copy hdiutil put in the image must still verify, or notarization will
# reject it after the upload.
mnt=$(mktemp -d)
if ! hdiutil attach -nobrowse -readonly -mountpoint "$mnt" "$out"; then
	echo "could not mount $out to verify the app inside"
	exit 1
fi
if ! codesign --verify --deep --strict --verbose=2 "$mnt/nexchat.app"; then
	echo "The app inside the DMG failed codesign --verify --deep --strict."
	exit 1
fi
hdiutil detach "$mnt" >/dev/null
rmdir "$mnt" || true
mnt=""
codesign --verify --deep --strict --verbose=2 "$out"

if [ "$notary_count" -eq 0 ]; then
	echo "made dist/$(basename "$out") (Developer ID signed, not notarized)"
	echo "Set NOTARY_KEY_PATH, NOTARY_KEY_ID and NOTARY_ISSUER_ID to notarize."
	exit 0
fi

notary_submit "$out"
xcrun stapler staple "$out"
xcrun stapler validate "$out"

echo "Verifying signatures and Gatekeeper assessments"
codesign --verify --deep --strict --verbose=2 "$app"
codesign --verify --deep --strict --verbose=2 "$out"
spctl --assess --type execute --verbose=2 "$app"
spctl --assess --type open --context context:primary-signature --verbose=2 "$out"
echo "made dist/$(basename "$out") (Developer ID, notarized, stapled)"

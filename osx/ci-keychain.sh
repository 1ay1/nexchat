#!/bin/sh
# Temporary keychain for the release workflow.
#
#   sh osx/ci-keychain.sh import
#   sh osx/ci-keychain.sh delete
#
# import reads these environment secrets. All six set: import the Developer ID
# Application .p12, write CODESIGN_IDENTITY / CODESIGN_KEYCHAIN / NOTARY_* into
# $GITHUB_ENV, and leave the .p8 at $RUNNER_TEMP/AuthKey.p8. None set: print
# that this run stays ad hoc and exit 0. A subset set: exit 1, so a half
# configured release is not published as if it were notarized.
#
#   MACOS_CERT_P12        base64 of the Developer ID Application .p12
#   MACOS_CERT_PASSWORD   password of that .p12
#   KEYCHAIN_PASSWORD     password for the temporary keychain (any random string)
#   NOTARY_KEY_P8         base64 of the App Store Connect API .p8
#   NOTARY_KEY_ID         the key's Key ID
#   NOTARY_ISSUER_ID      the issuer UUID from App Store Connect
#
# delete removes the keychain, the .p8 and the .p12. It is safe to run when
# import did nothing. See osx/SIGNING.md.
set -eu

cmd=${1:-}
root=${RUNNER_TEMP:-/tmp}
keychain=$root/nexchat-signing.keychain-db
p12=$root/nexchat-dev-id.p12
p8=$root/AuthKey.p8
saved=$root/nexchat-keychains.before

decode_b64() {
	python3 -c 'import sys,base64; sys.stdout.buffer.write(base64.b64decode(sys.stdin.buffer.read()))'
}

case $cmd in
import)
	names="MACOS_CERT_P12 MACOS_CERT_PASSWORD KEYCHAIN_PASSWORD NOTARY_KEY_P8 NOTARY_KEY_ID NOTARY_ISSUER_ID"
	missing=""
	present=0
	for name in $names; do
		eval "v=\${$name-}"
		if [ -n "$v" ]; then
			present=$((present + 1))
		else
			missing="$missing $name"
		fi
	done
	unset v
	if [ "$present" -eq 0 ]; then
		echo "No macOS signing secrets in the release environment."
		echo "The .app and the DMG will be ad hoc signed and not notarized."
		echo "See osx/SIGNING.md for the secrets the admin adds."
		echo "::notice title=macOS ad hoc::No signing secrets on the release environment. The DMG stays ad hoc, not notarized."
		exit 0
	fi
	if [ "$present" -ne 6 ]; then
		echo "::error title=macOS signing secrets::Set all of these or none of them:$missing. See osx/SIGNING.md."
		exit 1
	fi
	if [ -z "${GITHUB_ENV:-}" ]; then
		echo "ci-keychain.sh import writes to GITHUB_ENV. Locally, use your login keychain and export CODESIGN_IDENTITY. See osx/SIGNING.md."
		exit 1
	fi
	case $KEYCHAIN_PASSWORD in
	-*) echo "::error::KEYCHAIN_PASSWORD must not start with '-' (the security tool would read it as a flag)."; exit 1 ;;
	esac
	case $MACOS_CERT_PASSWORD in
	-*) echo "::error::MACOS_CERT_PASSWORD must not start with '-' (the security tool would read it as a flag)."; exit 1 ;;
	esac

	umask 077
	printf '%s' "$MACOS_CERT_P12" | decode_b64 > "$p12"
	printf '%s' "$NOTARY_KEY_P8" | decode_b64 > "$p8"
	chmod 600 "$p12" "$p8"
	if ! grep -q "PRIVATE KEY" "$p8"; then
		rm -f "$p12" "$p8"
		echo "::error::NOTARY_KEY_P8 did not decode to a PEM private key. Base64 the .p8 file Apple downloaded, armor and all."
		exit 1
	fi
	# the p12 is imported below; don't leave it if a later command fails
	trap 'rm -f "$p12"' EXIT

	if [ -e "$keychain" ]; then
		security delete-keychain "$keychain" || rm -f "$keychain"
	fi
	security create-keychain -p "$KEYCHAIN_PASSWORD" "$keychain"
	# stay unlocked for the job; the keychain is deleted at the end
	security set-keychain-settings -lut 21600 "$keychain"
	security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$keychain"

	# The runner image usually has these intermediates. Importing them into
	# the temporary keychain covers a .p12 that was exported without the chain.
	for url in \
		https://www.apple.com/certificateauthority/DeveloperIDG2CA.cer \
		https://www.apple.com/certificateauthority/DeveloperIDCA.cer
	do
		cer=$root/$(basename "$url")
		if curl -fsSL "$url" -o "$cer"; then
			security import "$cer" -k "$keychain" >/dev/null || \
				echo "warning: could not import $(basename "$url")"
		else
			echo "warning: could not download $(basename "$url"); the system keychain may already trust it"
		fi
		rm -f "$cer"
	done

	# -A: codesign runs non-interactively and cannot show a keychain prompt.
	# The keychain exists only for this job and delete removes it.
	security import "$p12" -k "$keychain" -P "$MACOS_CERT_PASSWORD" -f pkcs12 \
		-T /usr/bin/codesign -T /usr/bin/security -A
	rm -f "$p12"
	trap - EXIT

	security list-keychains -d user | sed 's/^[[:space:]]*//; s/"//g' > "$saved"
	# shellcheck disable=SC2046
	security list-keychains -d user -s "$keychain" $(cat "$saved")
	security set-key-partition-list -S apple-tool:,apple:,codesign: -s \
		-k "$KEYCHAIN_PASSWORD" "$keychain" >/dev/null

	ident=$(security find-identity -p codesigning -v "$keychain" \
		| sed -n 's/.*"\(Developer ID Application:[^"]*\)".*/\1/p' \
		| head -n 1)
	if [ -z "$ident" ]; then
		echo "::error::The .p12 has no Developer ID Application identity. Export the certificate and its private key, not a development certificate."
		security find-identity -p codesigning -v "$keychain" || true
		exit 1
	fi
	echo "Using signing identity: $ident"
	{
		echo "CODESIGN_IDENTITY=$ident"
		echo "CODESIGN_KEYCHAIN=$keychain"
		echo "NOTARY_KEY_PATH=$p8"
		echo "NOTARY_KEY_ID=$NOTARY_KEY_ID"
		echo "NOTARY_ISSUER_ID=$NOTARY_ISSUER_ID"
	} >> "$GITHUB_ENV"
	;;
delete)
	if [ -e "$keychain" ]; then
		security delete-keychain "$keychain" || rm -f "$keychain"
	fi
	if [ -f "$saved" ]; then
		# shellcheck disable=SC2046
		security list-keychains -d user -s $(cat "$saved") || true
		rm -f "$saved"
	fi
	rm -f "$p12" "$p8"
	;;
*)
	echo "usage: sh osx/ci-keychain.sh import|delete" >&2
	exit 1
	;;
esac

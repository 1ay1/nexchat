# Signing the macOS release

A `v*` tag builds `nexchat-<version>-macos-arm64.dmg`. With the secrets below on the `release` environment, that DMG is Developer ID signed, notarized and stapled. With none of the secrets set, the same job keeps today's ad hoc signature and says so in the log. Pull requests and the macOS build workflow never see the secrets.

The app is not sandboxed. Entitlements are `osx/nexchat.entitlements`, an empty plist. Why nothing is in it:

- Sandbox keys (`com.apple.security.network.client` and the file keys) only apply to a sandboxed app. nexchat is not one.
- GObject signal closures go through libffi. On Apple Silicon, Homebrew's libffi is built with `FFI_EXEC_TRAMPOLINE_TABLE`: the executable page is signed code inside `libffi`, remapped, not writable-and-executable memory. `com.apple.security.cs.allow-jit` and `allow-unsigned-executable-memory` are not required. Intel libffi maps RWX memory and would need `allow-unsigned-executable-memory`; this release is arm64 only.
- Libraries, plugins, gdk-pixbuf loaders and OpenSSL's provider modules are signed with the same Developer ID, which satisfies library validation. `disable-library-validation` is not set. An unsigned plugin dropped in later will not load. That is a product choice left open on purpose (the Mac release build also leaves out Perl, Python and Lua).

`com.apple.security.get-task-allow` must not be added. Apple rejects notarization when it is present.

Ad hoc signing does not use this plist, the hardened runtime, or a timestamp. `osx/check-app.sh` traces `DYLD_PRINT_LIBRARIES`, and the hardened runtime strips `DYLD_*`. The release job runs that check on the ad hoc bundle, then re-signs.

## Secrets

Add these as **environment secrets** on `release`, not as repository secrets. A repository secret would be readable by any workflow on this repo, including a pull request. Environment secrets are handed to a job only after the environment's rules pass, and only a job that names the environment can see them.

| Secret | What it is |
|---|---|
| `MACOS_CERT_P12` | base64 of the Developer ID Application `.p12` (certificate and private key) |
| `MACOS_CERT_PASSWORD` | password set when exporting that `.p12` |
| `KEYCHAIN_PASSWORD` | password for the temporary keychain CI creates. Not an Apple password. `openssl rand -base64 24` |
| `NOTARY_KEY_P8` | base64 of the App Store Connect API key (`.p8`) |
| `NOTARY_KEY_ID` | Key ID of that API key (10 characters) |
| `NOTARY_ISSUER_ID` | Issuer ID at the top of the App Store Connect API keys page (a UUID) |

All six, or none. A subset fails the macOS job so a half-configured tag is not published as a signed build. Passwords must not start with `-`. Do not commit the `.p12` or the `.p8`.

Create the environment and its rules **before** adding the secrets, so the secrets never sit on an environment that anyone can deploy to.

## Developer ID certificate

The account needs the Apple Developer Program. The certificate type is **Developer ID Application**. That is the one for an app distributed outside the Mac App Store. Developer ID Installer is for `.pkg` files. Apple Development and Mac App Distribution will not notarize a DMG.

1. On a Mac, open Keychain Access → Certificate Assistant → Request a Certificate From a Certificate Authority. Use the account's email, a common name, and "Saved to disk". The default 2048-bit RSA key is fine.
2. [developer.apple.com/account](https://developer.apple.com/account) → Certificates, Identifiers & Profiles → Certificates → `+` → Developer ID Application. Upload the CSR and download the `.cer`.
3. Double-click the `.cer` so it lands in the keychain that holds the private key. In My Certificates, "Developer ID Application: Your Name (TEAMID)" should expand to show a private key. If it does not, the CSR was made on another Mac.
4. Select the certificate and its private key, then File → Export Items. Format: Personal Information Exchange (`.p12`). The export password is `MACOS_CERT_PASSWORD`.

Xcode → Settings → Accounts → Manage Certificates → `+` → Developer ID Application creates the same certificate directly in the keychain. Export the `.p12` the same way.

```sh
base64 -i Certificates.p12 | tr -d '\n' | pbcopy   # macOS
base64 -w0 Certificates.p12                        # Linux
```

Paste that into `MACOS_CERT_P12`. Line wrapping is fine; CI strips it when decoding.

If `codesign` later says the identity is not trusted, the `.p12` was exported without its intermediate. CI also imports [Developer ID G2](https://www.apple.com/certificateauthority/DeveloperIDG2CA.cer) into the temporary keychain. The private key still has to be in the `.p12`.

## Notarization key

Notarization uses an App Store Connect API key, not an Apple ID password.

1. [appstoreconnect.apple.com](https://appstoreconnect.apple.com) → Users and Access → Integrations → App Store Connect API.
2. Under Team Keys, generate a key. Access: **Developer**. A lower role cannot notarize. Copy the Issuer ID from the top of that page and the Key ID from the row.
3. Download the `.p8`. Apple shows it once. The filename is `AuthKey_<Key ID>.p8`.

```sh
base64 -i AuthKey_XXXXXXXXXX.p8 | tr -d '\n' | pbcopy
```

That is `NOTARY_KEY_P8`. Base64 the file, including the `BEGIN PRIVATE KEY` lines. The Key ID is `NOTARY_KEY_ID`, the Issuer ID is `NOTARY_ISSUER_ID`.

## The `release` environment

Repository → Settings → Environments → New environment. Name it `release` (the workflow asks for that name).

- **Required reviewers:** Justin, GitHub user `web-connect`. He needs write access or the approval cannot be submitted. A tag push still builds Windows, Linux and the source tarball immediately; the macOS job waits here, and the GitHub Release is published only after that job finishes.
- **Deployment branches and tags:** Selected branches and tags → Add deployment branch or tag rule → Ref type **Tag**, name pattern `v*`. A `workflow_dispatch` from a branch does not match, so it cannot receive the secrets. To re-run a release, dispatch the workflow from the `v*` tag.

Then add the six secrets under **Environment secrets**.

Until this environment exists, the first tag push by an admin creates an empty one and the log says the DMG is ad hoc. Configure the reviewer and the tag rule before the tag you want signed.

## Who can push a `v*` tag

Settings → Rules → Rulesets → New ruleset → New tag ruleset.

- Enforcement: Active.
- Target tags: Include by pattern `v*`. Same match as the release workflow, so it covers every tag that would publish.
- Rules: Restrict creations, and Restrict updates so a tag cannot be moved.
- Bypass list: Repository role → Admin. Admins (the owner, `1ay1`, and anyone else with the Admin role) can still push, move and delete `v*` tags. Everyone else cannot.

`web-connect` is the deployment reviewer. He can push a version tag only if he is also an admin or is added to this bypass list. Restrict deletions as well if deleted tags should be admin-only; admins on the bypass list can still delete.

## What CI does

`.github/workflows/release.yml`, macOS job, `environment: release`:

1. `osx/ci-keychain.sh import` reads the six secrets. None set: it logs that this run is ad hoc and does not create a keychain. All six set: it decodes the `.p12` into `$RUNNER_TEMP/nexchat-signing.keychain-db`, allows `codesign` to use the key without a GUI prompt, and exports `CODESIGN_IDENTITY`. The `.p8` is decoded to `$RUNNER_TEMP/AuthKey.p8`.
2. The app is built and checked ad hoc (`ninja -C build app`, `osx/check-app.sh`).
3. With an identity, `osx/make-app.py build --sign "$CODESIGN_IDENTITY"` rebuilds the bundle and signs every Mach-O inside out (`--options runtime --timestamp`), then the bundle. Executables in `Contents/MacOS` get the entitlements file. `CODESIGN_KEYCHAIN` points `codesign` at the temporary keychain.
4. `osx/make-dmg.sh` builds the DMG. With notary credentials it submits a zip of the app, staples the app, signs the DMG, submits the DMG, staples it, and runs `codesign --verify --deep --strict` and `spctl --assess` on the app and the DMG.
5. `osx/ci-keychain.sh delete` always runs, including after a failed build, and removes the keychain, the `.p12` and the `.p8`.

The release notes say "notarized" only when an identity was present. They say "ad hoc" when the secrets were absent.

## Sign on your own Mac

Same build the release job uses (GTK from `osx/build-gtk.sh`, dbus and the scripting plugins off):

```sh
meson setup build -Ddbus=disabled -Dwith-perl=false -Dwith-python=false -Dwith-lua=false -Dtui=enabled \
  "-Dpkg_config_path=$HOME/.cache/nexchat-gtk/lib/pkgconfig"
ninja -C build
ninja -C build app
sh osx/check-app.sh build/nexchat.app
```

`security find-identity -v -p codesigning` prints the identity string. Then:

```sh
python3 osx/make-app.py build --sign "Developer ID Application: Your Name (TEAMID)"

export CODESIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"
export NOTARY_KEY_PATH="$HOME/AuthKey_XXXXXXXXXX.p8"
export NOTARY_KEY_ID="XXXXXXXXXX"
export NOTARY_ISSUER_ID="00000000-0000-0000-0000-000000000000"
sh osx/make-dmg.sh build/nexchat.app 0.2.1
```

Leave `CODESIGN_IDENTITY` unset to build the unsigned DMG. Set it and leave the `NOTARY_*` variables unset to sign the DMG without notarizing. `ninja -C build app` on its own is always ad hoc; it does not read `CODESIGN_IDENTITY`.

## If the signed app misbehaves

`codesign` says "resource fork, Finder information, or similar detritus not allowed": something under the bundle has an xattr. The Developer ID path runs `xattr -cr` on the bundle before signing.

`errSecInternalComponent` from `codesign`: the keychain was locked or the key's partition list does not include `codesign`. `osx/ci-keychain.sh` sets both. Locally, unlock the login keychain.

Notary status `Invalid`: the log URL is printed. The usual causes are a signature without a timestamp, a nested Mach-O that was not signed, or the hardened runtime missing on the main executable.

The app dies at launch inside `ffi_closure_alloc` or `vm_remap`: arm64 libffi was not the trampoline build. Add `com.apple.security.cs.allow-jit` to `osx/nexchat.entitlements` (both executables are signed with that file) and re-sign. Add `allow-unsigned-executable-memory` only if it still dies. Do not add either "just in case"; they weaken the hardened runtime, and an empty plist is what notarization of this bundle is set up for.

`codesign` rejects the entitlements file: the explanation lives in this document. The plist Apple reads is an empty `dict`.

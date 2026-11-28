#!/bin/sh
# nexchat installer for macOS and Linux:
#
#   curl -fsSL https://1ay1.github.io/nexchat/install.sh | sh
#
# Installs the latest release; run it again to update. Nothing needs root:
#   macOS   nexchat.app into /Applications (or ~/Applications)
#   Linux   the Flatpak, for this user only (GNOME runtime from Flathub)
# and a `nexchat` command in ~/.local/bin. Every download is checked against
# the release's SHA256SUMS.txt.
#
# Options (after `sh -s --` when piped):  --dry-run  --version 0.3.1
# Windows: irm https://1ay1.github.io/nexchat/install.ps1 | iex
set -eu

repo=https://github.com/1ay1/nexchat
dry=
want=
say() { printf '%s\n' "$*"; }
while [ $# -gt 0 ]; do
	case $1 in
		--dry-run) dry=1 ;;
		--version) want=${2#v}; shift ;;
		-h|--help) say "usage: install.sh [--dry-run] [--version X.Y.Z]   (piped: curl ... | sh -s -- --dry-run)"; exit 0 ;;
		*) echo "nexchat: unknown option $1" >&2; exit 2 ;;
	esac
	shift
done

if [ -t 1 ]; then b=$(printf '\033[1m'); d=$(printf '\033[2m'); r=$(printf '\033[0m'); else b='' d='' r=''; fi
step() { printf '%s==>%s %s\n' "$b" "$r" "$*"; }
die() { printf 'nexchat: %s\n' "$*" >&2; exit 1; }
run() { if [ -n "$dry" ]; then say "${d}would run:${r} $*"; else "$@"; fi; }
have() { command -v "$1" >/dev/null 2>&1; }

have curl || die "curl is needed"

tmp=$(mktemp -d)
mnt=
cleanup() { [ -n "$mnt" ] && hdiutil detach -quiet "$mnt" 2>/dev/null; rm -rf "$tmp"; }
trap cleanup EXIT INT TERM

# the latest version: where /releases/latest redirects to (no API, no rate limit)
if [ -z "$want" ]; then
	loc=$(curl -fsSI -o /dev/null -w '%{redirect_url}' "$repo/releases/latest") || die "can't reach GitHub"
	want=${loc##*/v}
	case $want in ''|*/*) die "couldn't find the latest release ($loc)" ;; esac
fi
dl=$repo/releases/download/v$want

# fetch FILE: download it into $tmp and check it against SHA256SUMS.txt
fetch() {
	step "Downloading $1"
	curl -fL --progress-bar -o "$tmp/$1" "$dl/$1" || die "download failed: $dl/$1"
	[ -f "$tmp/SHA256SUMS.txt" ] || curl -fsSL -o "$tmp/SHA256SUMS.txt" "$dl/SHA256SUMS.txt" ||
		die "no checksums for v$want"
	sum=$(awk -v f="$1" '{ sub(/^\.\//, "", $2) } $2 == f { print $1 }' "$tmp/SHA256SUMS.txt")
	[ -n "$sum" ] || die "$1 isn't in SHA256SUMS.txt"
	if have sha256sum; then got=$(sha256sum "$tmp/$1" | cut -d' ' -f1)
	else got=$(shasum -a 256 "$tmp/$1" | cut -d' ' -f1); fi
	[ "$got" = "$sum" ] || die "$1: checksum mismatch (got $got, want $sum)"
}

# a `nexchat` command in ~/.local/bin that runs what was installed
launcher() { # command...
	bin=$HOME/.local/bin
	run mkdir -p "$bin"
	if [ -e "$bin/nexchat" ] && ! grep -q 'made by the nexchat installer' "$bin/nexchat" 2>/dev/null; then
		say "${d}$bin/nexchat is yours, left alone${r}"; return
	fi
	if [ -z "$dry" ]; then
		printf '#!/bin/sh\n# made by the nexchat installer\nexec %s "$@"\n' "$*" > "$bin/nexchat"
		chmod +x "$bin/nexchat"
	else say "${d}would write:${r} $bin/nexchat -> $*"; fi
	case :$PATH: in *:"$bin":*) ;; *) path_note=1 ;; esac
}
path_note=
running=

os=$(uname -s); arch=$(uname -m)
say "${b}nexchat $want${r} ${d}for $os $arch${r}"

case $os in
Darwin)
	[ "$arch" = arm64 ] || die "there's no Intel Mac build yet: build from source ($repo#building)"
	f=nexchat-$want-macos-arm64.dmg
	fetch "$f"
	apps=/Applications
	[ -w "$apps" ] || { apps=$HOME/Applications; run mkdir -p "$apps"; }
	step "Installing into $apps"
	if [ -z "$dry" ]; then
		mnt=$(mktemp -d)
		hdiutil attach -quiet -nobrowse -readonly -mountpoint "$mnt" "$tmp/$f"
		# a running nexchat keeps the old version until it's restarted
		pgrep -x nexchat >/dev/null 2>&1 && running=1
		rm -rf "$apps/nexchat.app"
		ditto "$mnt/nexchat.app" "$apps/nexchat.app"
		hdiutil detach -quiet "$mnt"; mnt=
		# downloaded with curl, so not quarantined; clear it anyway in case
		# an earlier download through a browser left it on
		xattr -dr com.apple.quarantine "$apps/nexchat.app" 2>/dev/null || true
	else say "${d}would copy:${r} nexchat.app -> $apps/nexchat.app"; fi
	launcher "'$apps/nexchat.app/Contents/MacOS/nexchat'"
	open_with="open -a nexchat"
	;;
Linux)
	if [ "$arch" != x86_64 ]; then
		have pacman && die "no $arch package yet. On Arch: yay -S nexchat-git (AUR)"
		die "no $arch package yet: build from source ($repo#building)"
	fi
	if ! have flatpak; then
		say "nexchat for Linux is a Flatpak, and flatpak isn't installed. Install it, then run this again:"
		if have apt-get; then say "  sudo apt install flatpak"
		elif have dnf; then say "  sudo dnf install flatpak"
		elif have pacman; then say "  sudo pacman -S flatpak      ${d}(or skip Flatpak: yay -S nexchat-git)${r}"
		elif have zypper; then say "  sudo zypper install flatpak"
		elif have apk; then say "  sudo apk add flatpak"
		else say "  https://flatpak.org/setup/"; fi
		exit 1
	fi
	f=nexchat-$want-linux-x86_64.flatpak
	fetch "$f"
	step "Installing for $(id -un) (the first time also fetches the GNOME runtime, ~1 GB)"
	run flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
	run flatpak install --user --noninteractive --reinstall -y "$tmp/$f"
	launcher flatpak run io.github.nexchat
	open_with="nexchat  (or from your app menu)"
	;;
MINGW*|MSYS*|CYGWIN*)
	die "on Windows, in PowerShell: irm https://1ay1.github.io/nexchat/install.ps1 | iex"
	;;
*)
	die "no package for $os: build from source ($repo#building)"
	;;
esac

say ""
if [ -n "$dry" ]; then say "${b}Dry run:${r} nothing was installed (the downloads were checked)."; exit 0; fi
say "${b}nexchat $want is installed.${r} Start it: $open_with"
[ "$os" = Darwin ] && say "In a terminal: nexchat --tui"
[ -n "$path_note" ] && say "${d}Add ~/.local/bin to your PATH to type nexchat in a terminal.${r}"
[ -n "$running" ] && say "nexchat is running: quit and reopen it to use $want."
say "${d}Update: run this again. Your HexChat settings are picked up on first start.${r}"

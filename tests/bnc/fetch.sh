#!/bin/sh
# Download the bouncers the tests run against, into ~/.cache/nexchat-ref/bnc
# (no root): soju and ZNC from Arch's packages, Ergo's release build.
set -eu
# macOS: soju built with Go, Ergo's macOS release, ZNC from Homebrew
if [ "$(uname)" = Darwin ]; then
	dir=${BNC_MAC_HOME:-$HOME/.cache/nexchat-ref/bnc-mac}
	mkdir -p "$dir"
	cd "$dir"
	[ -x bin/soju ] || GOBIN=$dir/bin go install codeberg.org/emersion/soju/cmd/soju@v0.11.1
	[ -x bin/sojudb ] || GOBIN=$dir/bin go install codeberg.org/emersion/soju/cmd/sojudb@v0.11.1
	[ -x bin/sojuctl ] || GOBIN=$dir/bin go install codeberg.org/emersion/soju/cmd/sojuctl@v0.11.1
	arch=$(uname -m)
	[ -d "ergo-2.19.1-macos-$arch" ] || {
		curl -sfL -o ergo.tgz "https://github.com/ergochat/ergo/releases/download/v2.19.1/ergo-2.19.1-macos-$arch.tar.gz"
		tar xf ergo.tgz
	}
	brew list znc >/dev/null 2>&1 || brew install znc
	echo "bouncers in $dir (ZNC from Homebrew)"
	exit 0
fi
dir=${BNC_HOME:-$HOME/.cache/nexchat-ref/bnc}
mkdir -p "$dir"
cd "$dir"
mirror=${ARCH_MIRROR:-https://geo.mirror.pkgbuild.com}
fetch() { [ -f "$2" ] || curl -sfL -o "$2" "$1"; }
fetch "$mirror/extra/os/x86_64/soju-0.11.1-1-x86_64.pkg.tar.zst" soju.pkg.tar.zst
fetch "$mirror/extra/os/x86_64/znc-1.10.3-1-x86_64.pkg.tar.zst" znc.pkg.tar.zst
fetch https://github.com/ergochat/ergo/releases/download/v2.19.1/ergo-2.19.1-linux-x86_64.tar.gz ergo.tar.gz
echo "c275f2de8e8eb074c38707136392290172400b5b78616cb9f660351527924b47  ergo.tar.gz" | sha256sum -c -
for f in soju.pkg.tar.zst znc.pkg.tar.zst; do tar xf "$f" usr; done
tar xf ergo.tar.gz
echo "bouncers in $dir"

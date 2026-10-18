#!/bin/sh
# Download the bouncers the tests run against, into ~/.cache/nexchat-ref/bnc
# (no root): soju and ZNC from Arch's packages, Ergo's release build.
set -eu
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

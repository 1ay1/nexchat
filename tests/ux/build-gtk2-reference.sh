#!/bin/sh
# Build a private GTK 2.24 + the unmodified HexChat GTK2 frontend, for UX reference shots.
# Arch dropped gtk2, so it goes into ~/.cache/nexchat-ref/prefix.
set -eu
ref=${NX_REF:-$HOME/.cache/nexchat-ref}
root=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$ref"
cd "$ref"
if [ ! -f prefix/lib/pkgconfig/gtk+-2.0.pc ]; then
	[ -d gtk+-2.24.33 ] || curl -sSL https://download.gnome.org/sources/gtk+/2.24/gtk+-2.24.33.tar.xz | tar xJ
	cd gtk+-2.24.33
	# gtk2 doesn't build with gcc 15 defaults
	CFLAGS="-O1 -std=gnu11 -Wno-error -Wno-incompatible-pointer-types -Wno-int-conversion -Wno-implicit-function-declaration" \
		./configure --prefix="$ref/prefix" --disable-introspection --disable-cups --disable-papi \
		--disable-gtk-doc --disable-man >/dev/null
	make -j"$(nproc)" -C gdk && make -j"$(nproc)" -C gtk && make -j"$(nproc)" -C modules/engines
	make -C gdk install && make -C gtk install && make -C modules/engines install && make install-pkgconfigDATA
	cp gtk+-2.0.pc gdk-2.0.pc "$ref/prefix/lib/pkgconfig/"
	cd ..
fi
# reference = the last upstream commit, built from a worktree so the port can't leak in
[ -d hexchat-src ] || git -C "$root" worktree add --detach "$ref/hexchat-src" b544ac33
PKG_CONFIG_PATH="$ref/prefix/lib/pkgconfig" meson setup "$ref/hexchat-build" "$ref/hexchat-src" \
	-Dwith-perl=false -Dwith-python=false -Dwith-lua=false -Dc_args=-Wno-incompatible-pointer-types >/dev/null
ninja -C "$ref/hexchat-build" src/fe-gtk/hexchat
echo "reference: LD_LIBRARY_PATH=$ref/prefix/lib $ref/hexchat-build/src/fe-gtk/hexchat"

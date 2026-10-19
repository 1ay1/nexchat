#!/bin/sh
# Build the GTK nexchat's macOS app bundles, with the fixes in
# osx/gtk-patches/ applied, into a prefix of its own.
#
# GTK's macOS backend thaws a surface's updates every time it's shown
# again without ever re-freezing them when hidden (osx/gtk-patches/
# 0001-*.patch, sent upstream): "gdk_surface_thaw_updates: assertion
# 'surface->update_freeze_count > 0' failed" on every popover, tooltip and
# dialog shown twice. GTK main (since 4.25, the "Rework throttling" change)
# plus that patch is clean; 4.24.x also has the old frame-timing bugs.
#
# usage: osx/build-gtk.sh [PREFIX]   (default ~/.cache/nexchat-gtk)
# then:  PKG_CONFIG_PATH=PREFIX/lib/pkgconfig meson setup build ...
set -eu
here=$(cd "$(dirname "$0")" && pwd)
prefix=${1:-$HOME/.cache/nexchat-gtk}
# the GTK commit the patches are made against
ref=${GTK_REF:-15f6e05b2c}
src=${GTK_SRC:-$HOME/.cache/nexchat-gtk-src}

if [ ! -d "$src/.git" ]; then
	git clone --filter=blob:none https://gitlab.gnome.org/GNOME/gtk.git "$src"
fi
cd "$src"
git fetch -q origin
git checkout -q --force "$ref"
git clean -qfdx -e _build
for p in "$here"/gtk-patches/*.patch; do
	git apply "$p"
done

meson setup _build --prefix="$prefix" --reconfigure \
	-Dmacos-backend=true -Dx11-backend=false -Dwayland-backend=false \
	-Dmedia-gstreamer=disabled -Dprint-cups=disabled -Dvulkan=disabled \
	-Dintrospection=disabled -Ddocumentation=false -Dman-pages=false \
	-Dbuild-tests=false -Dbuild-testsuite=false -Dbuild-examples=false -Dbuild-demos=false
ninja -C _build
ninja -C _build install
# nexchat looks for this next to the GTK it runs on: these GTK bugs are
# fixed here, so it hides none of GTK's messages
mkdir -p "$prefix/share/nexchat"
( cd "$here/gtk-patches" && ls *.patch ) > "$prefix/share/nexchat/gtk-patched"
echo "GTK with nexchat's patches in $prefix"
echo "build nexchat with: PKG_CONFIG_PATH=$prefix/lib/pkgconfig meson setup build ..."

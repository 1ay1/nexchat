#!/bin/sh
# Point the Windows sysroot's glib tool variables (glib-compile-resources,
# glib-genmarshal, ...) at the Linux ones: they run at build time, on the
# build machine. Run once after tests/win/msys2-sysroot.py.
# usage: tests/win/fix-sysroot.sh [SYSROOT]   (default ~/.cache/nexchat-win/ucrt64)
set -eu
s=${1:-$HOME/.cache/nexchat-win/ucrt64}
for pc in "$s/lib/pkgconfig/gio-2.0.pc" "$s/lib/pkgconfig/glib-2.0.pc"; do
	sed -i \
		-e "s|^glib_compile_schemas=.*|glib_compile_schemas=$(command -v glib-compile-schemas)|" \
		-e "s|^glib_compile_resources=.*|glib_compile_resources=$(command -v glib-compile-resources)|" \
		-e "s|^gdbus_codegen=.*|gdbus_codegen=$(command -v gdbus-codegen)|" \
		-e "s|^glib_genmarshal=.*|glib_genmarshal=$(command -v glib-genmarshal)|" \
		-e "s|^glib_mkenums=.*|glib_mkenums=$(command -v glib-mkenums)|" \
		"$pc"
done
# MSYS2's .pc files use /ucrt64 paths; make them point into the sysroot
for pc in "$s"/lib/pkgconfig/*.pc; do
	sed -i -E "s#(^|[=[:space:]'\"]|-I|-L)/ucrt64(/|\$|[[:space:]])#\1$s\2#g" "$pc"
done
echo "fixed $s"

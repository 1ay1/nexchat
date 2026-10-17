#!/usr/bin/env python3
"""Move the pointer to each X,Y and print the cursor's name there (XFixes).

usage: DISPLAY=:94 cursor.py X,Y [X,Y ...]
Prints one "X,Y name" line per point; "-" for an unnamed (default) cursor.
"""
import ctypes
import ctypes.util
import sys
import time


def load(name, fallbacks):
    for so in fallbacks:
        try:
            return ctypes.CDLL(so)
        except OSError:
            pass
    path = ctypes.util.find_library(name)
    if path:
        return ctypes.CDLL(path)
    raise SystemExit(f"cursor.py: no lib{name}")


x11 = load("X11", ["libX11.so.6"])
xtst = load("Xtst", ["libXtst.so.6"])
xfixes = load("Xfixes", ["libXfixes.so.3"])


class CursorImage(ctypes.Structure):
    _fields_ = [("x", ctypes.c_short), ("y", ctypes.c_short),
                ("width", ctypes.c_ushort), ("height", ctypes.c_ushort),
                ("xhot", ctypes.c_ushort), ("yhot", ctypes.c_ushort),
                ("cursor_serial", ctypes.c_ulong),
                ("pixels", ctypes.c_void_p),
                ("atom", ctypes.c_ulong),
                ("name", ctypes.c_char_p)]


x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
x11.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
x11.XFree.argtypes = [ctypes.c_void_p]
xtst.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                                      ctypes.c_int, ctypes.c_ulong]
xfixes.XFixesGetCursorImage.restype = ctypes.POINTER(CursorImage)
xfixes.XFixesGetCursorImage.argtypes = [ctypes.c_void_p]

dpy = x11.XOpenDisplay(None)
if not dpy:
    raise SystemExit("cursor.py: can't open display")
for arg in sys.argv[1:]:
    x, y = (int(v) for v in arg.split(","))
    xtst.XTestFakeMotionEvent(dpy, -1, x, y, 0)
    x11.XSync(dpy, 0)
    time.sleep(0.5)
    img = xfixes.XFixesGetCursorImage(dpy)
    name = "-"
    if img:
        c = img.contents
        if c.name:
            name = c.name.decode() or "-"
        # unnamed (core/theme-less) cursors: size, hotspot and pixel hash
        px = ctypes.cast(c.pixels, ctypes.POINTER(ctypes.c_ulong))
        h = hash(tuple(px[i] & 0xffffffff for i in range(c.width * c.height))) & 0xffff
        name += f" {c.width}x{c.height}@{c.xhot},{c.yhot}#{h:04x}"
    print(f"{arg} {name}")
    if img:
        x11.XFree(img)

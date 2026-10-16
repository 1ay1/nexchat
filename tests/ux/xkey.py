#!/usr/bin/env python3
"""Send key presses to an X display through XTest, stdlib only (ctypes).

A stand-in for `xdotool key` on machines without xdotool (e.g. macOS with
XQuartz's Xvfb). Each argument is a key combo like "ctrl+s", "alt+e", "p",
"Return", "Escape"; names are X keysym names.

usage: DISPLAY=:94 xkey.py [--delay 0.3] ctrl+s Escape
       --click/--rclick/--mclick/--dblclick/--wheel-up/--wheel-down X,Y
       --drag X1,Y1:X2,Y2   (left button)
"""
import ctypes
import ctypes.util
import os
import sys
import time


def load(name, fallbacks):
    for path in [ctypes.util.find_library(name)] + fallbacks:
        if path:
            try:
                return ctypes.CDLL(path)
            except OSError:
                pass
    raise SystemExit(f"cannot load lib{name}")


x11 = load("X11", ["/opt/X11/lib/libX11.6.dylib", "libX11.so.6"])
xtst = load("Xtst", ["/opt/X11/lib/libXtst.6.dylib", "libXtst.so.6"])

x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
x11.XStringToKeysym.restype = ctypes.c_ulong
x11.XStringToKeysym.argtypes = [ctypes.c_char_p]
x11.XKeysymToKeycode.restype = ctypes.c_ubyte
x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
x11.XFlush.argtypes = [ctypes.c_void_p]
x11.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
xtst.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
xtst.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
xtst.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
x11.XSetInputFocus.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]

POINTER_ROOT = 1
REVERT_TO_POINTER_ROOT = 1


def focus_at(dpy, x, y):
    """Bare Xvfb has no window manager, so nothing has keyboard focus.
    PointerRoot focus + moving the pointer onto the window gives it the keys,
    which is what a click-less WM would do."""
    xtst.XTestFakeMotionEvent(dpy, -1, x, y, 0)
    x11.XSetInputFocus(dpy, POINTER_ROOT, REVERT_TO_POINTER_ROOT, 0)
    x11.XSync(dpy, 0)
    # the toolkit sees FocusIn asynchronously; keys sent before it lands are
    # dropped (the first characters of --type went missing without this)
    time.sleep(0.5)

MODS = {"ctrl": "Control_L", "control": "Control_L", "alt": "Alt_L",
        "shift": "Shift_L", "super": "Super_L", "meta": "Meta_L"}


def keycode(dpy, name):
    sym = x11.XStringToKeysym(MODS.get(name.lower(), name).encode())
    if not sym:
        raise SystemExit(f"unknown key {name!r}")
    code = x11.XKeysymToKeycode(dpy, sym)
    if not code:
        raise SystemExit(f"no keycode for {name!r} on this display")
    return code


def press(dpy, combo):
    codes = [keycode(dpy, k) for k in combo.split("+")]
    for c in codes:
        xtst.XTestFakeKeyEvent(dpy, c, 1, 0)
        x11.XSync(dpy, 0)
    time.sleep(0.02)
    for c in reversed(codes):
        xtst.XTestFakeKeyEvent(dpy, c, 0, 0)
        x11.XSync(dpy, 0)


def type_text(dpy, text, delay):
    """Type literal text (ASCII), shifting for capitals and the usual symbols."""
    shifted = {"!": "exclam", "@": "at", "#": "numbersign", "$": "dollar",
               "%": "percent", "^": "asciicircum", "&": "ampersand", "*": "asterisk",
               "(": "parenleft", ")": "parenright", "_": "underscore", "+": "plus",
               ":": "colon", "?": "question", "<": "less", ">": "greater", "~": "asciitilde",
               '"': "quotedbl", "{": "braceleft", "}": "braceright", "|": "bar"}
    names = {" ": "space", "/": "slash", ".": "period", ",": "comma", "-": "minus",
             "=": "equal", ";": "semicolon", "'": "apostrophe", "\\": "backslash",
             "[": "bracketleft", "]": "bracketright", "`": "grave"}
    for ch in text:
        if ch in names:
            press(dpy, names[ch])
        elif ch in shifted:
            press(dpy, "shift+" + shifted[ch])
        elif ch.isupper():
            press(dpy, "shift+" + ch.lower())
        else:
            press(dpy, ch)
        time.sleep(delay)


def main(argv):
    if argv[:1] == ["--wait-display"]:
        # exit 0 once the display takes connections (Xvfb is up)
        for _ in range(100):
            dpy = x11.XOpenDisplay(os.environ.get("DISPLAY", "").encode() or None)
            if dpy:
                x11.XCloseDisplay(dpy)
                return
            time.sleep(0.1)
        raise SystemExit("display did not come up")
    delay = 0.3
    at = (300, 200)
    text = None
    clicks = []
    while argv[:1] and argv[0].startswith("--"):
        if argv[0] == "--type":
            text = argv[1]
        if argv[0] in ("--click", "--rclick", "--mclick", "--dblclick", "--wheel-up", "--wheel-down"):
            kind = {"--click": (1, 1), "--rclick": (3, 1), "--mclick": (2, 1), "--dblclick": (1, 2),
                    "--wheel-up": (4, 1), "--wheel-down": (5, 1)}[argv[0]]
            clicks.append(tuple(int(v) for v in argv[1].split(",")) + kind)
        if argv[0] == "--drag":
            a, b = argv[1].split(":")
            clicks.append(tuple(int(v) for v in a.split(",")) + (1, -1)
                          + tuple(int(v) for v in b.split(",")))
        if argv[0] == "--delay":
            delay = float(argv[1])
        elif argv[0] == "--at":
            at = tuple(int(v) for v in argv[1].split(","))
        argv = argv[2:]
    dpy = x11.XOpenDisplay(os.environ.get("DISPLAY", "").encode() or None)
    if not dpy:
        raise SystemExit("cannot open display")
    focus_at(dpy, *at)
    for cx, cy, button, count, *to in clicks:
        xtst.XTestFakeMotionEvent(dpy, -1, cx, cy, 0)
        x11.XSync(dpy, 0)
        time.sleep(0.1)
        if count < 0:  # drag: press, move in steps, release at `to`
            xtst.XTestFakeButtonEvent(dpy, button, 1, 0)
            x11.XSync(dpy, 0)
            for i in range(1, 11):
                time.sleep(0.03)
                xtst.XTestFakeMotionEvent(dpy, -1, cx + (to[0] - cx) * i // 10,
                                          cy + (to[1] - cy) * i // 10, 0)
                x11.XSync(dpy, 0)
            # hover at the target (one more motion, then wait) before the
            # release: GTK4's XDND handshake needs a position at the drop site
            time.sleep(0.1)
            xtst.XTestFakeMotionEvent(dpy, -1, to[0], to[1] + 1, 0)
            x11.XSync(dpy, 0)
            time.sleep(0.4)
            xtst.XTestFakeButtonEvent(dpy, button, 0, 0)
            x11.XSync(dpy, 0)
            time.sleep(delay)
            continue
        for _ in range(count):
            xtst.XTestFakeButtonEvent(dpy, button, 1, 0)
            xtst.XTestFakeButtonEvent(dpy, button, 0, 0)
            x11.XSync(dpy, 0)
            time.sleep(0.05)
        time.sleep(delay)
    # pointer stays at the last click so a popup menu opened there stays up
    if text is not None:
        type_text(dpy, text, 0.03)
    for combo in argv:
        press(dpy, combo)
        time.sleep(delay)


if __name__ == "__main__":
    main(sys.argv[1:])

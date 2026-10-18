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
    # the plain sonames first: ctypes.util.find_library runs ldconfig/gcc
    # and cost ~0.8s on every call
    for path in fallbacks + [None]:
        if path is None:
            path = ctypes.util.find_library(name)
        if path:
            try:
                return ctypes.CDLL(path)
            except OSError:
                pass
    raise SystemExit(f"cannot load lib{name}")


x11 = load("X11", ["libX11.so.6", "/opt/X11/lib/libX11.6.dylib"])
xtst = load("Xtst", ["libXtst.so.6", "/opt/X11/lib/libXtst.6.dylib"])

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
    if argv[:1] == ["--steal-focus"]:
        # a plain window named nx-focus-thief, kept open for argv[1] seconds
        # (a WM gives it focus when activated)
        dpy = x11.XOpenDisplay(os.environ.get("DISPLAY", "").encode() or None)
        x11.XDefaultRootWindow.restype = ctypes.c_ulong
        x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
        x11.XCreateSimpleWindow.restype = ctypes.c_ulong
        x11.XCreateSimpleWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong] + [ctypes.c_int] * 4 + \
            [ctypes.c_uint, ctypes.c_ulong, ctypes.c_ulong]
        x11.XMapWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        x11.XStoreName.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_char_p]
        w = x11.XCreateSimpleWindow(dpy, x11.XDefaultRootWindow(dpy), 700, 450, 200, 100, 1, 0, 0xffffff)
        x11.XStoreName(dpy, w, b"nx-focus-thief")
        x11.XMapWindow(dpy, w)
        x11.XFlush(dpy)
        time.sleep(float(argv[1]) if len(argv) > 1 else 30)
        return
    if argv[:1] == ["--wait-window"]:
        # exit 0 once a top-level window is mapped (the app is up), 20s max
        dpy = None
        for _ in range(100):
            dpy = x11.XOpenDisplay(os.environ.get("DISPLAY", "").encode() or None)
            if dpy:
                break
            time.sleep(0.1)
        if not dpy:
            raise SystemExit("cannot open display")
        x11.XDefaultRootWindow.restype = ctypes.c_ulong
        x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
        x11.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong),
                                   ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
                                   ctypes.POINTER(ctypes.c_uint)]

        class Attrs(ctypes.Structure):
            _fields_ = [("x", ctypes.c_int), ("y", ctypes.c_int), ("width", ctypes.c_int),
                        ("height", ctypes.c_int), ("border_width", ctypes.c_int), ("depth", ctypes.c_int),
                        ("visual", ctypes.c_void_p), ("root", ctypes.c_ulong), ("class_", ctypes.c_int),
                        ("bit_gravity", ctypes.c_int), ("win_gravity", ctypes.c_int),
                        ("backing_store", ctypes.c_int), ("backing_planes", ctypes.c_ulong),
                        ("backing_pixel", ctypes.c_ulong), ("save_under", ctypes.c_int),
                        ("colormap", ctypes.c_ulong), ("map_installed", ctypes.c_int),
                        ("map_state", ctypes.c_int), ("all_event_masks", ctypes.c_long),
                        ("your_event_mask", ctypes.c_long), ("do_not_propagate_mask", ctypes.c_long),
                        ("override_redirect", ctypes.c_int), ("screen", ctypes.c_void_p)]
        x11.XGetWindowAttributes.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(Attrs)]
        root = x11.XDefaultRootWindow(dpy)
        for _ in range(200):
            r, p = ctypes.c_ulong(), ctypes.c_ulong()
            kids, n = ctypes.POINTER(ctypes.c_ulong)(), ctypes.c_uint()
            x11.XQueryTree(dpy, root, ctypes.byref(r), ctypes.byref(p), ctypes.byref(kids), ctypes.byref(n))
            for i in range(n.value):
                a = Attrs()
                # IsViewable, and bigger than GTK's 1x1/10x10 helper windows
                if x11.XGetWindowAttributes(dpy, kids[i], ctypes.byref(a)) and \
                        a.map_state == 2 and a.width > 50 and a.height > 50:
                    return
            time.sleep(0.1)
        raise SystemExit("no window came up")
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
        elif argv[0] == "--hold":
            # KEY:SECONDS: held down, repeating 30 times a second like a
            # real keyboard (press events without releases between)
            k, s = argv[1].split(":")
            clicks.append(("hold", k, float(s)))
        elif argv[0] == "--move":
            clicks.append(("move",) + tuple(int(v) for v in argv[1].split(",")))
        argv = argv[2:]
    dpy = x11.XOpenDisplay(os.environ.get("DISPLAY", "").encode() or None)
    if not dpy:
        raise SystemExit("cannot open display")
    focus_at(dpy, *at)
    for c in clicks:
        if c[0] == "hold":
            code = keycode(dpy, c[1])
            end = time.time() + c[2]
            while time.time() < end:
                xtst.XTestFakeKeyEvent(dpy, code, 1, 0)
                x11.XSync(dpy, 0)
                time.sleep(1 / 30)
            xtst.XTestFakeKeyEvent(dpy, code, 0, 0)
            x11.XSync(dpy, 0)
            time.sleep(delay)
            continue
        if c[0] == "move":
            xtst.XTestFakeMotionEvent(dpy, -1, c[1], c[2], 0)
            x11.XSync(dpy, 0)
            time.sleep(delay)
            continue
        cx, cy, button, count, *to = c
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

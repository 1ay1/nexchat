#!/usr/bin/env python3
"""Render nexchat-tui to a PNG: drive it in a pty (pyte as the terminal)
against fake_ircd --showcase, run some steps, then paint every cell with
its real colours in Menlo. For looking at the TUI the way a person does.

    tui-png.py BIN OUT.png [--size 120x34] [--theme NAME] [--prefs "k = v\\n..."] -- STEP...

STEP: keys (<esc> <enter> <tab> <up> <down> <f2> <f10> <c-x> <a-x>),
click:X,Y[,button], move:X,Y (hover), sleep:S. Needs Pillow (a venv: ~/.cache/nexchat-shotenv).
"""
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from importlib import import_module
smoke = import_module("tui-smoke")

from PIL import Image, ImageDraw, ImageFont

NAMED = {
    "black": (0x1d, 0x1f, 0x21), "red": (0xcc, 0x66, 0x66), "green": (0xb5, 0xbd, 0x68), "brown": (0xf0, 0xc6, 0x74),
    "blue": (0x81, 0xa2, 0xbe), "magenta": (0xb2, 0x94, 0xbb), "cyan": (0x8a, 0xbe, 0xb7), "white": (0xc5, 0xc8, 0xc6),
    "brightblack": (0x96, 0x98, 0x96), "brightred": (0xde, 0x93, 0x5f), "brightgreen": (0xa3, 0xbe, 0x8c),
    "brightbrown": (0xf0, 0xc6, 0x74), "brightblue": (0x81, 0xa2, 0xbe), "brightmagenta": (0xb4, 0x8e, 0xad),
    "brightcyan": (0x88, 0xc0, 0xd0), "brightwhite": (0xff, 0xff, 0xff),
}
TERM_BG, TERM_FG = (0x1d, 0x1f, 0x21), (0xc5, 0xc8, 0xc6)
KEYS = {"<esc>": "\x1b", "<enter>": "\r", "<tab>": "\t", "<stab>": "\x1b[Z", "<up>": "\x1b[A", "<down>": "\x1b[B",
        "<right>": "\x1b[C", "<left>": "\x1b[D", "<f10>": "\x1b[21~", "<f2>": "\x1bOQ", "<f1>": "\x1bOP",
        "<pgup>": "\x1b[5~", "<pgdn>": "\x1b[6~", "<bs>": "\x7f", "<space>": " "}


def color(c, default):
    if c in (None, "default"):
        return default
    if c in NAMED:
        return NAMED[c]
    if len(c) == 6:
        try:
            return tuple(int(c[i:i + 2], 16) for i in (0, 2, 4))
        except ValueError:
            pass
    return default


def render(screen, out, cols, rows):
    font = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 15, index=0)
    bold = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", 15, index=1)
    cw, ch = 9, 19
    img = Image.new("RGB", (cols * cw + 16, rows * ch + 16), TERM_BG)
    d = ImageDraw.Draw(img)
    for y in range(rows):
        row = screen.buffer[y]
        for x in range(cols):
            c = row[x]
            fg, bg = color(c.fg, TERM_FG), color(c.bg, TERM_BG)
            if c.reverse:
                fg, bg = bg, fg
            px, py = 8 + x * cw, 8 + y * ch
            if bg != TERM_BG:
                d.rectangle([px, py, px + cw - 1, py + ch - 1], fill=bg)
    for y in range(rows):
        row = screen.buffer[y]
        for x in range(cols):
            c = row[x]
            fg, bg = color(c.fg, TERM_FG), color(c.bg, TERM_BG)
            if c.reverse:
                fg, bg = bg, fg
            px, py = 8 + x * cw, 8 + y * ch
            if c.data and c.data != " ":
                d.text((px, py + 1), c.data, font=bold if c.bold else font, fill=fg)
            if c.underscore:
                d.line([px, py + ch - 3, px + cw - 1, py + ch - 3], fill=fg)
    img.save(out)


def main():
    a = sys.argv[1:]
    binary, out = os.path.abspath(a[0]), a[1]
    a = a[2:]
    size, theme, prefs = "120x34", "", ""
    while a and a[0] != "--":
        k = a.pop(0)
        if k == "--size":
            size = a.pop(0)
        elif k == "--theme":
            theme = a.pop(0)
        elif k == "--prefs":
            prefs = a.pop(0).replace("\\n", "\n")
    steps = a[1:] if a else []
    cols, rows = (int(v) for v in size.split("x"))
    smoke.COLS, smoke.ROWS = cols, rows

    tmp = tempfile.mkdtemp()
    log, portfile = os.path.join(tmp, "s.log"), os.path.join(tmp, "port")
    ircd = subprocess.Popen([sys.executable, os.path.join(os.path.dirname(HERE), "fake_ircd.py"), "--log", log,
                             "--portfile", portfile, "--showcase"])
    for _ in range(100):
        if os.path.exists(portfile) and open(portfile).read().strip():
            break
        time.sleep(0.05)
    port = open(portfile).read().strip()
    cfg = os.path.join(tmp, "cfg")
    os.makedirs(cfg)
    open(os.path.join(cfg, "hexchat.conf"), "w").write(
        "net_throttle = 0\nirc_nick1 = noor\ngui_slist_skip = 1\nurl_grabber = 1\n" + prefs)
    if theme:
        open(os.path.join(cfg, "tui.conf"), "w").write(f"[tui]\ntheme={theme}\n")
    os.environ.pop("NO_COLOR", None)
    t = smoke.Tui([binary, "-n", "-d", cfg, f"irc://127.0.0.1:-{port}/"],
                  {"TERM": "xterm-256color", "COLORTERM": "truecolor"})
    try:
        t.pump(3.0)
        for s in steps:
            if s.startswith("sleep:"):
                t.pump(float(s[6:]))
            elif s.startswith("move:"):
                v = [int(n) for n in s[5:].split(",")]
                t.send(f"\x1b[<35;{v[0] + 1};{v[1] + 1}M", 0.3)
            elif s.startswith("click:"):
                v = [int(n) for n in s[6:].split(",")]
                t.click(v[0], v[1], button=v[2] if len(v) > 2 else 0)
            elif s.startswith("<c-") and s.endswith(">"):
                t.send(chr(ord(s[3]) & 0x1f), 0.4)
            elif s.startswith("<a-") and s.endswith(">"):
                t.send("\x1b" + s[3:-1], 0.4)
            elif s in KEYS:
                t.send(KEYS[s], 0.4)
            else:
                t.type(s, 0.4)
        t.pump(0.6)
        render(t.screen, out, cols, rows)
        print(out)
    finally:
        t.stop()
        ircd.terminate()


if __name__ == "__main__":
    main()

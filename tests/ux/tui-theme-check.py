#!/usr/bin/env python3
"""Every glyph the TUI draws under a maya scheme is in that scheme's colours.

    tui-theme-check.py GALLERY [THEME...]

Runs the gallery in a pty (pyte as the terminal), visits each pane and the
switcher, and checks every non-blank cell: an explicit foreground (never
the terminal's own, "default", which nobody checked against the scheme's
canvas) and the scheme's background (or a colour on it, for a selection
or the caret). Native is checked too: there every colour must be one of
the terminal's own sixteen or default, never RGB.
"""
import os
import pty
import select
import signal
import struct
import sys
import time
import fcntl
import termios

import pyte

COLS, ROWS = 110, 24
KEYS = [b"", b"hi\x1b[D", b"\x1bOQ", b"\x1bOR", b"\x07", b"nex"]   # -, type+←, F2, F3, Ctrl-G, type
# Rules and frames: drawn in the theme's Border, which maya's schemes make
# deliberately faint (a line is seen, not read).
LINES = set("─│╭╮╰╯")


def lum(hexrgb):
    def ch(v):
        s = v / 255.0
        return s / 12.92 if s <= 0.03928 else ((s + 0.055) / 1.055) ** 2.4
    r, g, b = (int(hexrgb[i:i + 2], 16) for i in (0, 2, 4))
    return 0.2126 * ch(r) + 0.7152 * ch(g) + 0.0722 * ch(b)


def contrast(a, b):
    try:
        x, y = lum(a), lum(b)
    except ValueError:
        return 21.0                                  # a named colour: not ours to judge
    if x < y:
        x, y = y, x
    return (x + 0.05) / (y + 0.05)


def run(gallery, theme, keys):
    screen = pyte.Screen(COLS, ROWS)
    stream = pyte.ByteStream(screen)
    pid, fd = pty.fork()
    if pid == 0:
        os.environ.pop("NO_COLOR", None)
        os.environ["TERM"] = "xterm-256color"
        os.environ["COLORTERM"] = "truecolor"
        os.execv(gallery, [gallery, "--theme", theme])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))

    def pump(t):
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], max(0, end - time.time()))
            if not r:
                continue
            try:
                data = os.read(fd, 65536)
            except OSError:
                return
            if b"\x1b[5n" in data:
                os.write(fd, b"\x1b[0n")
            stream.feed(data)

    pump(0.6)
    for k in keys:
        if k:
            os.write(fd, k)
        pump(0.25)
    try:
        os.kill(pid, signal.SIGTERM)
        os.waitpid(pid, 0)
    except OSError:
        pass
    return screen


def main():
    gallery = sys.argv[1]
    themes = sys.argv[2:] or ["native", "Atom One Light", "Dracula", "Catppuccin Latte", "Gruvbox Dark"]
    named = {"black", "red", "green", "brown", "blue", "magenta", "cyan", "white",
             "brightblack", "brightred", "brightgreen", "brightbrown", "brightblue",
             "brightmagenta", "brightcyan", "brightwhite", "default"}
    bad = 0
    for theme in themes:
        for i in range(len(KEYS)):
            screen = run(gallery, theme, KEYS[: i + 1])
            canvas = screen.buffer[ROWS // 2][COLS // 2].bg      # the chat area's background
            problems = []
            for y in range(ROWS):
                for x in range(COLS):
                    c = screen.buffer[y][x]
                    if theme == "native":
                        if c.fg not in named or c.bg not in named:
                            problems.append(f"{x},{y} {c.data!r} fg={c.fg} bg={c.bg}: not the terminal's palette")
                        continue
                    if c.data.strip() == "":
                        continue
                    if c.fg == "default":
                        problems.append(f"{x},{y} {c.data!r}: the terminal's own foreground")
                    if c.bg == "default":
                        problems.append(f"{x},{y} {c.data!r}: the terminal's own background")
                    if c.reverse:
                        problems.append(f"{x},{y} {c.data!r}: reverse video")
                    if c.fg == c.bg:
                        problems.append(f"{x},{y} {c.data!r}: fg == bg {c.fg}")
                    elif c.data not in LINES and contrast(c.fg, c.bg) < 1.5:
                        problems.append(f"{x},{y} {c.data!r}: unreadable, {c.fg} on {c.bg} "
                                        f"({contrast(c.fg, c.bg):.2f}:1)")
            if canvas == "default" and theme != "native":
                problems.append("the scheme didn't paint its canvas")
            if problems:
                bad += 1
                print(f"{theme} (step {i}): {len(problems)} cell(s)")
                for p in problems[:5]:
                    print("   ", p)
    if bad:
        sys.exit(1)
    print(f"tui theming: {len(themes)} themes x {len(KEYS)} screens clean")


if __name__ == "__main__":
    main()

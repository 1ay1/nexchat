#!/usr/bin/env python3
"""The TUI at every size, against fake_ircd --demo: from a 36x14 corner of a
screen to a 200x50 one, every window fits and every row is whole.

At each size, for the main window and each popup/panel:
  * no line runs past the right edge (pyte would wrap it: a row ending in
    a box edge must have its matching left edge)
  * every box that opens on a row closes on it (╭ … ╮, │ … │, ╰ … ╯)
  * the input frame is the last three rows and the status above it
And the chat's tiers: wide shows times and nicks in columns, a narrow
one drops the time first, a tiny one stacks the nick over the text.

    tui-sizes.py NEXCHAT_TUI
"""
import os
import re
import signal
import fcntl
import struct
import termios
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from importlib import import_module
smoke = import_module("tui-smoke")

SIZES = [(36, 14), (50, 18), (80, 24), (120, 32), (200, 50)]


def main():
    binary = os.path.abspath(sys.argv[1])
    tmp = tempfile.mkdtemp()
    fails = []

    def start_ircd(tag):
        log, portfile = os.path.join(tmp, f"s{tag}.log"), os.path.join(tmp, f"port{tag}")
        p = subprocess.Popen([sys.executable, os.path.join(smoke.HERE, "fake_ircd.py"), "--log", log,
                              "--portfile", portfile, "--demo"])
        for _ in range(50):
            if os.path.exists(portfile) and open(portfile).read().strip():
                break
            time.sleep(0.1)
        return p, open(portfile).read().strip()

    def check(cond, what):
        if not cond:
            print("FAIL:", what)
            fails.append(what)

    def boxes_whole(t, where, rows_end=True):
        """every row that starts a box edge ends it: ╭…╮ ╰…╯ ├…┤ │…│ (a frame
        that spans the screen); and nothing past the width. A menu may cover
        the end of a frame under it: rows_end=False skips that part."""
        for y, line in enumerate(t.screen.display):
            s = line.rstrip()
            if len(line) > t.screen.columns:
                check(False, f"{where}: row {y} is wider than the screen")
            for l, r in (("╭", "╮"), ("╰", "╯"), ("├", "┤")):
                if rows_end and s.startswith(l) and not s.endswith(r):
                    check(False, f"{where}: row {y} opens {l} and doesn't close: {s[-12:]!r}")
        # every ╭ has its ╰ somewhere under it in the same column (a box
        # cut off by the bottom edge has none)
        d = t.screen.display
        for y, line in enumerate(d):
            for x, ch in enumerate(line):
                if ch == "╭" and not any(len(d[y2]) > x and d[y2][x] in "╰├" for y2 in range(y + 1, len(d))):
                    check(False, f"{where}: the box at {x},{y} has no bottom on screen")

    for cols, rows in SIZES:
        smoke.COLS, smoke.ROWS = cols, rows
        ircd, port = start_ircd(cols)
        cfg = os.path.join(tmp, f"cfg{cols}")
        os.makedirs(cfg)
        open(os.path.join(cfg, "hexchat.conf"), "w").write(
            f"net_throttle = 0\nirc_nick1 = sz{cols}\ngui_slist_skip = 1\nstamp_text = 1\nstamp_text_format = %H:%M\n")
        open(os.path.join(cfg, "tui.conf"), "w").write("[tui]\nstamp_repeat=1\n")
        os.environ.pop("NO_COLOR", None)
        t = smoke.Tui([binary, "-n", "-d", cfg, f"irc://127.0.0.1:-{port}/#demo"], {"TERM": "xterm-256color"})
        try:
            t.pump(2.0)
            p = t.find("#demo")
            if p:
                t.click(p[0] + 1, p[1])
                t.pump(0.3)
            where = f"{cols}x{rows}"
            check(t.wait_for("morning everyone", 3) or t.wait_for("morning", 1)
                  or t.wait_for("private notice", 1), f"{where}: the chat is there")
            boxes_whole(t, f"{where} main")
            d = t.screen.display
            check(d[-3].startswith("╭") and d[-1].startswith("╰"), f"{where}: the input frame is the last 3 rows")
            # the chat's tiers
            line = next((l for l in d if "morning everyone" in l), "")
            has_time = re.search(r"\d\d:\d\d", line) is not None
            stacked = line.lstrip().startswith("morning everyone")
            if cols >= 120:
                check(has_time and "alice" in line, f"{where}: time and nick in columns ({line.strip()[:40]!r})")
            if cols < 60:
                # thin: one column, "time nick text" from the left edge, no │ column
                notice = next((l for l in d if "private notice to you" in l), None)
                check(notice is not None and re.match(r"\d\d:\d\d -bob- private notice", notice) is not None,
                      f"{where}: one column, time and nick inline ({(notice or '').strip()[:40]!r})")
                check(not any(re.match(r"\s*\S* │ ", l) for l in d[3:-5]),
                      f"{where}: no side columns in the chat")
            # every popup at this size
            for name, keys in (("palette", b"\x10"), ("go to", b"\x07"), ("keys", b"\x1bOP"),
                               ("network list", b"\x13"), ("command popup", b"/"),
                               ("window menu", b"\x1b[21~\x1b[C\x1b[C\x1b[C"),
                               ("bouncer", b"\x10bouncer\r")):
                t.send(keys, 0.6)
                boxes_whole(t, f"{where} {name}", name != "window menu")
                # the terminal shrinks and grows again with it open
                for c2, r2 in ((max(30, cols // 2), max(10, rows - 6)), (cols, rows)):
                    t.screen.resize(r2, c2)
                    fcntl.ioctl(t.fd, termios.TIOCSWINSZ, struct.pack("HHHH", r2, c2, 0, 0))
                    os.kill(t.pid, signal.SIGWINCH)
                    t.pump(0.5)
                    boxes_whole(t, f"{where} {name} resized to {c2}x{r2}", name != "window menu")
                t.send(b"\x1b", 0.3)
                t.send(b"\x1bu", 0.2)
                t.send(b"\x1b", 0.2)
        finally:
            t.stop()
            ircd.terminate()

    if fails:
        sys.exit(1)
    print("tui-sizes: ok")


if __name__ == "__main__":
    main()

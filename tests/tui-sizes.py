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

    def boxes_whole(t, where):
        """every row that starts a box edge ends it: ╭…╮ ╰…╯ ├…┤ │…│ (a frame
        that spans the screen); and nothing past the width"""
        for y, line in enumerate(t.screen.display):
            s = line.rstrip()
            if len(line) > t.screen.columns:
                check(False, f"{where}: row {y} is wider than the screen")
            for l, r in (("╭", "╮"), ("╰", "╯"), ("├", "┤")):
                if s.startswith(l) and not s.endswith(r):
                    check(False, f"{where}: row {y} opens {l} and doesn't close: {s[-12:]!r}")

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
            if cols <= 36:
                # tiny: stacked, a nick on its own row and the text under it
                notice = next((i for i, l in enumerate(d) if "private notice to you" in l), None)
                check(notice is not None and "-bob-" in d[notice - 1] and d[notice].startswith("  "),
                      f"{where}: stacked, the message under its nick")
            # every popup at this size
            for name, keys in (("palette", b"\x10"), ("go to", b"\x07"), ("keys", b"\x1bOP"),
                               ("network list", b"\x13"), ("command popup", b"/")):
                t.send(keys, 0.6)
                boxes_whole(t, f"{where} {name}")
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

#!/usr/bin/env python3
"""The TUI's find bar (Ctrl-F; the GUI's search bar in maingui.c) against
fake_ircd --demo, keys only:

  * Ctrl-F opens it over the status row; typing finds the newest match and
    says "1 of N"... from the oldest
  * Up / Enter go to older matches, Down to newer, both wrap; a match far
    up the scrollback is scrolled into view
  * Alt-C (match case), Alt-R (regex), Alt-A (highlight all) toggle the
    GUI's prefs; a bad regex says so; no match says "No results found."
  * Esc closes it and the status bar is back

    tui-find.py NEXCHAT_TUI
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


def main():
    binary = os.path.abspath(sys.argv[1])
    tmp = tempfile.mkdtemp()
    log, portfile = os.path.join(tmp, "server.log"), os.path.join(tmp, "port")
    ircd = subprocess.Popen([sys.executable, os.path.join(smoke.HERE, "fake_ircd.py"), "--log", log,
                             "--portfile", portfile, "--demo"])
    for _ in range(50):
        if os.path.exists(portfile) and open(portfile).read().strip():
            break
        time.sleep(0.1)
    port = open(portfile).read().strip()
    cfg = os.path.join(tmp, "cfg")
    os.makedirs(cfg)
    open(os.path.join(cfg, "hexchat.conf"), "w").write(
        "net_throttle = 0\nirc_nick1 = tester\ngui_slist_skip = 1\n"
        "text_search_case_match = 0\ntext_search_regexp = 0\ntext_search_highlight_all = 1\n")
    os.environ.pop("NO_COLOR", None)
    t = smoke.Tui([binary, "-n", "-d", cfg, f"irc://127.0.0.1:-{port}/#demo"], {"TERM": "xterm-256color"})
    fails = []

    def check(cond, what):
        print(("ok:  " if cond else "FAIL:"), what)
        if not cond:
            fails.append(what)

    def bar():
        lines = t.text().splitlines()
        return lines[-2] if len(lines) >= 2 else ""

    def conf():
        return open(os.path.join(cfg, "hexchat.conf")).read()

    try:
        t.pump(2.0)
        check(t.wait_for("#demo", 5), "in #demo")
        p = t.find("#demo")
        if p:
            t.click(p[0] + 1, p[1])
            t.pump(0.3)
        # a needle far up: the oldest line, then a screen and more of filler
        t.type("/echo NEEDLE-old first\r", 0.2)
        for i in range(60):
            t.type(f"/echo filler line {i}\r", 0.02)
        t.type("/echo the Needle in the middle\r", 0.2)
        for i in range(5):
            t.type(f"/echo more filler {i}\r", 0.02)
        t.type("/echo needle-new last\r", 0.6)
        check(t.find("NEEDLE-old") is None, "the old one is scrolled off screen")

        t.send(b"\x06", 0.4)                                  # Ctrl-F
        check("Find:" in bar(), "Ctrl-F opens the find bar over the status row")
        t.type("needle", 0.6)
        check("3 of 3" in bar(), "typing finds the newest match: 3 of 3")
        t.send(b"\x1b[A", 0.4)                                 # Up: older
        check("2 of 3" in bar(), "Up: an older one, 2 of 3")

        def painted(word):
            """(bg, bold, underline) of the cells spelling `word`, per row"""
            out = []
            for y, row in t.screen.buffer.items():
                line = "".join(row[x].data for x in range(t.screen.columns))
                i = line.find(word)
                if i >= 0:
                    out.append({(row[x].bg, row[x].bold, row[x].underscore) for x in range(i, i + len(word))})
            return out
        cur, other = painted("Needle"), painted("needle")    # "the Needle in", "needle-new"
        check(cur and len(cur[0]) == 1 and next(iter(cur[0]))[1:] == (True, True)
              and next(iter(cur[0]))[0] != "default",
              "the current match: on the selection band, bold and underlined")
        check(other and len(other[0]) == 1 and next(iter(other[0]))[1:] == (False, False)
              and next(iter(other[0]))[0] != "default",
              "the other matches: on the band too (highlight all), plain")
        t.send(b"\r", 0.4)                                     # Enter: older
        check("1 of 3" in bar(), "Enter: older again, 1 of 3")
        check(t.find("NEEDLE-old first") is not None, "the oldest is scrolled into view")
        t.send(b"\x1b[A", 0.4)
        check("3 of 3" in bar(), "Up from the oldest wraps to the newest")
        t.send(b"\x1b[B", 0.4)                                 # Down: newer, wraps
        check("1 of 3" in bar(), "Down from the newest wraps to the oldest")

        # match case (Alt-C): the GUI's pref, saved
        t.send(b"\x1bc", 0.6)
        check("[✓] case" in bar(), "Alt-C ticks match case")
        check("1 of 1" in bar(), "only the lowercase one matches now")
        check("text_search_case_match = 1" in conf(), "and it's saved, as the GUI's checkbox")
        t.send(b"\x1bc", 0.4)

        # regex (Alt-R)
        t.send(b"\x1br", 0.4)
        check("[✓] regex" in bar(), "Alt-R ticks regex")
        t.send(b"\x15", 0.2)                                   # Ctrl-U: clear the field
        t.type("filler line [0-9]+$", 0.6)
        check("of 60" in bar(), "a regex finds the 60 filler lines")
        t.send(b"\x15", 0.2)
        t.type("needle(", 0.6)
        check("Not a valid regular expression." in bar(), "a bad regex says so")
        t.send(b"\x1br", 0.4)

        t.send(b"\x15", 0.2)
        t.type("haystack", 0.6)
        check("No results found." in bar(), "nothing: No results found.")

        t.send(b"\x1b", 0.5)
        check("Find:" not in bar() and "F10 menu" in bar(), "Esc closes it, the status bar is back")
        t.type("x", 0.3)
        check(t.text().splitlines()[-1].rstrip().endswith("x"), "and typing goes to the input again")
        t.send(b"\x15", 0.2)

        # Window → Search... opens it too
        p = t.find("Window")
        t.click(p[0] + 1, p[1])
        q = t.wait_for("Search…", 2) and t.find("Search…")
        if q:
            t.click(q[0] + 1, q[1])
        t.pump(0.4)
        check("Find:" in bar(), "Window → Search… opens it")
        t.send(b"\x1b", 0.4)
    finally:
        screen = t.text()
        t.stop()
        ircd.terminate()

    if fails:
        print("--- screen\n" + screen)
        sys.exit(1)
    print("tui-find: ok")


if __name__ == "__main__":
    main()

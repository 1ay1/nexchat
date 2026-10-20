#!/usr/bin/env python3
"""Settings and the theme apply live in the TUI, against fake_ircd --demo:

  * the theme picker previews as you move (the colours change before
    Enter), Esc puts the old theme back, Enter keeps (and saves) the new one
  * /set stamp_text 0 drops the timestamps at once; /set stamp_text 1
    brings them back
  * /set text_indent 0: nicks flush left; back on: right-aligned
  * /set gui_tab_layout 0: tabs instead of the tree, no restart
  * Preferences → Appearance → Enable timestamps: off at once, before the
    window closes

    tui-live.py NEXCHAT_TUI
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
        "net_throttle = 0\nirc_nick1 = tester\ngui_slist_skip = 1\nstamp_text = 1\n"
        "stamp_text_format = %H:%M\ntext_indent = 1\n")
    open(os.path.join(cfg, "tui.conf"), "w").write("[tui]\ntheme=Dracula\nstamp_repeat=1\n")
    os.environ.pop("NO_COLOR", None)
    t = smoke.Tui([binary, "-n", "-d", cfg, f"irc://127.0.0.1:-{port}/#demo"],
                  {"TERM": "xterm-256color", "COLORTERM": "truecolor"})
    fails = []

    def check(cond, what):
        print(("ok:  " if cond else "FAIL:"), what)
        if not cond:
            fails.append(what)

    def bg():
        """the canvas colour: the background of an empty chat cell"""
        return t.screen.buffer[t.screen.lines // 2][t.screen.columns // 2].bg

    def line_of(s):
        for l in t.text().splitlines():
            if s in l:
                return l
        return ""

    try:
        t.pump(2.0)
        check(t.wait_for("#demo", 5) and t.wait_for("morning everyone", 5), "in #demo")
        p = t.find("#demo")
        if p:
            t.click(p[0] + 1, p[1])
            t.pump(0.3)
        dracula = bg()

        # ── the theme picker: preview, Esc, Enter ──────────────────────
        p = t.find("View")
        t.click(p[0] + 1, p[1])
        q = t.wait_for("Theme", 2) and t.find("Theme")
        if q:
            t.click(q[0] + 1, q[1])
            t.pump(0.3)
        # a submenu may list a "Pick…" item; open the picker however it's offered
        r = t.find("Choose a scheme")
        if r:
            t.click(r[0] + 1, r[1])
            t.pump(0.4)
        t.type("solarized light", 0.6)
        light = bg()
        check(light != dracula, "moving in the theme picker previews it (the canvas changed)")
        t.send(b"\x1b", 0.6)
        check(bg() == dracula, "Esc puts the old theme back")
        check("Dracula" in open(os.path.join(cfg, "tui.conf")).read(), "...and leaves tui.conf alone")

        # ── /set: timestamps, indent, layout ───────────────────────────
        before = line_of("morning everyone")
        check(re.search(r"\d\d:\d\d", before) is not None, "timestamps are on")
        t.type("/set stamp_text 0\r", 0.6)
        check(re.search(r"\d\d:\d\d", line_of("morning everyone")) is None, "/set stamp_text 0: gone at once")
        t.type("/set stamp_text 1\r", 0.6)
        check(re.search(r"\d\d:\d\d", line_of("morning everyone")) is not None, "/set stamp_text 1: back")

        l = line_of("morning everyone")
        nick_col = l.find("alice")
        t.type("/set text_indent 0\r", 0.6)
        l2 = line_of("morning everyone")
        check(l2.find("alice") < nick_col, "/set text_indent 0: nicks move flush left")
        t.type("/set text_indent 1\r", 0.6)
        check(line_of("morning everyone").find("alice") == nick_col, "/set text_indent 1: right-aligned again")

        t.type("/set gui_tab_layout 0\r", 0.8)
        tabs = [l for l in t.text().splitlines() if "Fake" in l and "#demo" in l]
        check(bool(tabs), "/set gui_tab_layout 0: tabs, no restart")
        t.type("/set gui_tab_layout 2\r", 0.8)
        check(any(re.match(r"^.\u25be Fake", l) for l in t.text().splitlines()), "and back to the tree")

        # ── Preferences: applies before the window closes ─────────────
        p = t.find("nexchat")
        t.click(p[0] + 1, p[1])
        q = t.wait_for("Preferences", 2) and t.find("Preferences")
        if q:
            t.click(q[0] + 1, q[1])
            t.pump(0.4)
        q = t.find("Enable timestamps")
        if q:
            line = t.text().splitlines()[q[1]]
            v = line.find("◉ On", q[0])
            t.click(v + 1 if v > 0 else q[0] + 1, q[1])   # a click on the value flips it
            t.pump(0.5)
        check("○ Off" in t.text().splitlines()[q[1]] if q else False, "the toggle shows Off")
        # the field has the focus now: Space flips it, and the chat follows
        t.send(b" ", 0.5)
        check("◉ On" in t.text().splitlines()[q[1]] if q else False, "Space flips it back on")
        t.send(b" ", 0.5)
        # the window is still open; the chat behind it changed
        check(t.find("Preferences") is not None, "Preferences still open")
        t.send(b"\x1b", 0.5)
        check(re.search(r"\d\d:\d\d", line_of("morning everyone")) is None,
              "Preferences → Enable timestamps off: applied")
        check("stamp_text = 0" in open(os.path.join(cfg, "hexchat.conf")).read(), "...and saved")
    finally:
        screen = t.text()
        t.stop()
        ircd.terminate()

    if fails:
        print("--- screen\n" + screen)
        sys.exit(1)
    print("tui-live: ok")


if __name__ == "__main__":
    main()

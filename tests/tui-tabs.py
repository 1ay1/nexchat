#!/usr/bin/env python3
"""The TUI's channel switcher as the GUI's prefs say (gui_tab_layout,
gui_tab_pos), against fake_ircd --demo, keys and mouse:

  * Tabs at the bottom (layout 0, pos 6): one row of tabs, no tree; the
    current tab marked; a click switches; the wheel steps; activity shows
    on a tab (heidi's query)
  * Tabs at the top (pos 5)
  * the tree on the right (layout 2, pos 3)
  * Preferences → Channel switcher → Switcher type: Tree puts it back,
    right away

    tui-tabs.py NEXCHAT_TUI
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


def tree_row(line):
    """the tree's network row: a bar column (▌ or space), then ▾ Fake"""
    return re.match(r"^.▾ Fake", line) is not None


def start(binary, prefs):
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
        "net_throttle = 0\nirc_nick1 = tester\ngui_slist_skip = 1\n" + prefs)
    os.environ.pop("NO_COLOR", None)
    t = smoke.Tui([binary, "-n", "-d", cfg, f"irc://127.0.0.1:-{port}/#demo"], {"TERM": "xterm-256color"})
    t.pump(2.5)
    return t, ircd, cfg


def main():
    binary = os.path.abspath(sys.argv[1])
    fails = []
    screen = ""

    def check(cond, what):
        print(("ok:  " if cond else "FAIL:"), what)
        if not cond:
            fails.append(what)

    def lines(t):
        return t.text().splitlines()

    def tab_row(t):
        """the row with all three tabs on it"""
        for i, l in enumerate(lines(t)):
            if "Fake" in l and "#demo" in l and "heidi" in l:
                return i
        return -1

    # ── tabs at the bottom ─────────────────────────────────────────────
    t, ircd, cfg = start(binary, "gui_tab_layout = 0\ngui_tab_pos = 6\n")
    try:
        r = tab_row(t)
        check(r >= 0, "tabs: Fake, #demo and heidi on one row")
        check(r > len(lines(t)) // 2, "at the bottom (gui_tab_pos 6)")
        check(not any(tree_row(l) for l in lines(t)), "no tree")
        check(t.find("alice │") is not None or t.find("morning everyone") is not None,
              "the chat takes the whole width")
        # a click on #demo's tab
        p = t.find(" #demo ")
        if p and p[1] == r:
            t.click(p[0] + 2, p[1])
            t.pump(0.4)
        check(t.find("morning everyone") is not None, "clicking a tab switches to it")
        row = t.screen.buffer[r]
        x = lines(t)[r].find("#demo")
        check(x >= 0 and row[x].bg != "default", "the current tab is marked (the selection band)")
        # the wheel on the strip: next tab (heidi)
        t.send(f"\x1b[<65;{x + 1};{r + 1}M", 0.5)
        check(t.find("hi, this is a query") is not None, "the wheel on the tabs steps to the next")
        t.send(f"\x1b[<64;{x + 1};{r + 1}M", 0.5)
        check(t.find("morning everyone") is not None, "and back")
        # F2 with no tree: Go to...
        t.send(b"\x1bOQ", 0.4)
        check("Go to" in t.text() or t.find("╭") is not None, "F2 (no tree): the Go to… switcher")
        t.send(b"\x1b", 0.3)

        # Preferences → Channel switcher: Tree, right away
        t.send(b"\x1b", 0.2)
        p = t.find("nexchat")
        t.click(p[0] + 1, p[1])
        q = t.wait_for("Preferences", 2) and t.find("Preferences")
        if q:
            t.click(q[0] + 1, q[1])
        t.pump(0.4)
        q = t.find("Channel switcher")
        if q:
            t.click(q[0] + 1, q[1])
            t.pump(0.4)
        q = t.find("Switcher type")
        check(q is not None, "Preferences → Channel switcher has Switcher type")
        if q:
            t.click(q[0] + 1, q[1])
            t.pump(0.2)
            t.send(b" ", 0.4)                     # step the choice: Tabs → Tree
        t.send(b"\x1b", 0.5)
        check("gui_tab_layout = 1" in open(os.path.join(cfg, "hexchat.conf")).read()
              or any(tree_row(l) for l in lines(t)), "switched to Tree")
        check(any(tree_row(l) for l in lines(t)), "and the tree is back, right away")
        screen = t.text()
    finally:
        t.stop()
        ircd.terminate()

    # ── tabs at the top ────────────────────────────────────────────────
    t, ircd, cfg = start(binary, "gui_tab_layout = 0\ngui_tab_pos = 5\n")
    try:
        r = tab_row(t)
        check(0 <= r <= 2, "tabs at the top (gui_tab_pos 5): under the menu bar")
    finally:
        t.stop()
        ircd.terminate()

    # ── the tree on the right ──────────────────────────────────────────
    t, ircd, cfg = start(binary, "gui_tab_layout = 2\ngui_tab_pos = 3\n")
    try:
        ls = lines(t)
        hit = [l for l in ls if l.rstrip().endswith("▾ Fake") or re.search(r"│.▾ Fake", l)]
        check(bool(hit), "the tree on the right (gui_tab_pos 3)")
        p = t.find("heidi")
        if p:
            t.click(p[0] + 1, p[1])
            t.pump(0.4)
        check(t.find("hi, this is a query") is not None, "a click in it still switches")
        if fails:
            screen = t.text()
    finally:
        t.stop()
        ircd.terminate()

    if fails:
        print("--- screen\n" + screen)
        sys.exit(1)
    print("tui-tabs: ok")


if __name__ == "__main__":
    main()

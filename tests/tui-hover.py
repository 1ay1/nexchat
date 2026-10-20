#!/usr/bin/env python3
"""Hover in the TUI (mode 1003 motion), against fake_ircd --demo: what a
click would act on is underlined while the pointer is over it, and only then.

  * a link in the chat; a nick in the nick column; plain text: nothing
  * a tab in the tree; a nick in the user list
  * a menu-bar title; the status bar's "F1 keys"
  * the tree's divider lights up
  * in a panel: a list row, a button, the ✕
  * the pointer leaves: every underline goes

    tui-hover.py NEXCHAT_TUI
"""
import os
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
    script = os.path.join(tmp, "script")
    # someone starts typing in #demo (IRCv3 +typing, a TAGMSG); judy never
    # speaks in the demo chatter, so nothing clears it
    open(script, "w").write("@+typing=active :judy!j@example.org TAGMSG #demo\n")
    ircd = subprocess.Popen([sys.executable, os.path.join(smoke.HERE, "fake_ircd.py"), "--log", log,
                             "--portfile", portfile, "--demo", "--script", script,
                             "--caps", "message-tags"])
    for _ in range(50):
        if os.path.exists(portfile) and open(portfile).read().strip():
            break
        time.sleep(0.1)
    port = open(portfile).read().strip()
    cfg = os.path.join(tmp, "cfg")
    os.makedirs(cfg)
    open(os.path.join(cfg, "hexchat.conf"), "w").write(
        "net_throttle = 0\nirc_nick1 = tester\ngui_slist_skip = 1\ngui_typing_show = 1\n")
    os.environ.pop("NO_COLOR", None)
    t = smoke.Tui([binary, "-n", "-d", cfg, f"irc://127.0.0.1:-{port}/#demo"], {"TERM": "xterm-256color"})
    fails = []

    def check(cond, what):
        print(("ok:  " if cond else "FAIL:"), what)
        if not cond:
            fails.append(what)

    def move(x, y):
        """bare motion: SGR button 35 (motion, no button), 1-based"""
        t.send(f"\x1b[<35;{x + 1};{y + 1}M", 0.3)

    def underlined(x, y):
        return t.screen.buffer[y][x].underscore

    def any_underline(rows=None):
        out = []
        for yy in rows if rows is not None else range(t.screen.lines):
            for xx in range(t.screen.columns):
                c = t.screen.buffer[yy][xx]
                if c.underscore and c.data.strip():
                    out.append((xx, yy))
        return out

    try:
        t.pump(2.0)
        check(t.wait_for("hexchat.github.io", 5), "in #demo")
        p = t.find("#demo")
        if p:
            t.click(p[0] + 1, p[1])
            t.pump(0.3)
        # ── motion: someone typing, the dots walk ──────────────────────
        # (first: a typer goes stale after 6 s)
        status_row = t.status_row()

        def dots():
            row = t.screen.buffer[status_row]
            line = "".join(row[x].data for x in range(t.screen.columns))
            i = line.find("•••")
            return None if i < 0 else tuple(row[i + k].fg for k in range(3))
        check("is typing" in t.text().splitlines()[status_row], "the status bar says judy is typing")
        d1 = dots()
        t.pump(0.2)
        d2 = dots()
        t.pump(0.2)
        d3 = dots()
        check(d1 is not None and len({d1, d2, d3}) > 1, "its dots move (an animation frame every ~120 ms)")

        # the URL is drawn underlined already (a link); find a nick instead
        base = set(any_underline())

        # ── the chat: a nick in the nick column ────────────────────────
        p = t.find("<alice>")
        check(p is not None, "alice is on screen")
        if p:
            move(p[0] + 2, p[1])
            check(underlined(p[0] + 2, p[1]), "hovering a nick in the chat underlines it")
            check(not underlined(p[0], p[1]), "...the nick, not its < >")
        # plain text: nothing new
        p = t.find("morning everyone")
        if p:
            move(p[0] + 2, p[1])
            check(set(any_underline()) - base == set(), "hovering plain text underlines nothing")

        # ── the tree and the user list ─────────────────────────────────
        p = t.find("heidi")
        if p:
            move(p[0] + 1, p[1])
            check(underlined(p[0] + 1, p[1]), "hovering a tab in the tree underlines it")
        p = t.find("mallory")
        if p:
            move(p[0] + 1, p[1])
            check(underlined(p[0] + 1, p[1]), "hovering a nick in the user list underlines it")

        # ── the menu bar and the status bar ────────────────────────────
        p = t.find("Window")
        move(p[0] + 1, p[1])
        check(underlined(p[0] + 1, p[1]), "hovering a menu title underlines it")
        lines = t.text().splitlines()
        sy = t.status_row()
        sx = lines[sy].find("keys")
        if sx > 0:
            move(sx, sy)
            check(underlined(sx, sy), "hovering F1 keys in the status bar underlines it")
            t.click(sx, sy)
            t.pump(0.4)
            check(t.wait_for("Keyboard Shortcuts", 2), "...and clicking it shows the keys")
            t.send(b"\r", 0.4)

        # ── the tree's divider ─────────────────────────────────────────
        row = len(lines) // 2
        div = next((x for x in range(30) if t.screen.buffer[row][x].data == "│"), None)
        if div is not None:
            before = t.screen.buffer[row][div].fg
            move(div, row)
            check(t.screen.buffer[row][div].fg != before, "hovering the tree's divider lights it up")

        # ── leaving: nothing stays ─────────────────────────────────────
        p = t.find("morning everyone")
        move(p[0] + 2, p[1])
        check(set(any_underline()) - base == set(), "moving away clears every hover")

        # ── a panel: Network List ──────────────────────────────────────
        t.send(b"\x13", 0.6)                       # Ctrl-S
        p = t.find("2600net")
        if p:
            move(p[0] + 1, p[1])
            check(underlined(p[0] + 1, p[1]), "hovering a row in a panel's list underlines it")
        p = t.find("Remove")
        if p:
            move(p[0] + 1, p[1])
            check(underlined(p[0] + 1, p[1]), "hovering a panel button underlines it")
        x_close = next((i for i, c in enumerate(t.text().splitlines()[1]) if c == "✕"), None)
        if x_close is not None:
            before = t.screen.buffer[1][x_close].fg
            move(x_close, 1)
            check(t.screen.buffer[1][x_close].fg != before, "hovering ✕ lights it up")
        t.send(b"\x1b", 0.4)
    finally:
        screen = t.text()
        t.stop()
        ircd.terminate()

    if fails:
        print("--- screen\n" + screen)
        sys.exit(1)
    print("tui-hover: ok")


if __name__ == "__main__":
    main()

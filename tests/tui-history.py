#!/usr/bin/env python3
"""Scrolled up to the first line, the TUI asks the server for what came
before it (CHATHISTORY BEFORE) and puts it on top, a page at a time, until
the server has nothing older:

  * PgUp to the top: a BEFORE goes out, its lines show above the rest, in
    order, without moving what was on screen
  * again: the next page, older still, on top of that
  * an empty answer: no more asking

    tui-history.py NEXCHAT_TUI
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
                             "--portfile", portfile, "--demo", "--history-pages", "2",
                             "--caps", "batch draft/chathistory server-time message-tags"])
    for _ in range(50):
        if os.path.exists(portfile) and open(portfile).read().strip():
            break
        time.sleep(0.1)
    port = open(portfile).read().strip()
    cfg = os.path.join(tmp, "cfg")
    os.makedirs(cfg)
    open(os.path.join(cfg, "hexchat.conf"), "w").write(
        "net_throttle = 0\nirc_nick1 = tester\ngui_slist_skip = 1\n")
    os.environ.pop("NO_COLOR", None)
    smoke.COLS, smoke.ROWS = 100, 16
    t = smoke.Tui([binary, "-n", "-d", cfg, f"irc://127.0.0.1:-{port}/#demo"], {"TERM": "xterm-256color"})
    fails = []

    def check(cond, what):
        print(("ok:  " if cond else "FAIL:"), what)
        if not cond:
            fails.append(what)

    def befores():
        return len(re.findall(r"CHATHISTORY BEFORE #demo timestamp=", open(log).read()))

    def to_top(n=6):
        for _ in range(n):
            t.send(b"\x1b[5~", 0.25)
        t.pump(0.8)

    try:
        t.pump(2.0)
        check(t.wait_for("#demo", 5), "in #demo")
        check(befores() == 0, "nothing asked while at the bottom")
        to_top()
        check(befores() >= 1, "at the top: CHATHISTORY BEFORE")
        to_top()
        check(t.wait_for("older-p2-0", 3), "the older lines came")
        to_top()
        check(t.wait_for("older-p1-0", 3), "and the next page, on top of them")
        to_top()
        text = t.text()
        p1, p2 = text.find("older-p1-2"), text.find("older-p2-0")
        check(p1 != -1 and p2 != -1 and p1 < p2, "the older page sits above the newer one")
        n = befores()
        to_top()
        to_top()
        check(befores() == n, "after an empty answer, no more asking")
        sb = []
        for root, _, files in os.walk(os.path.join(cfg, "scrollback")):
            sb += [open(os.path.join(root, f), errors="replace").read() for f in files]
        check(any("hey tester" in s or "morning" in s for s in sb), "the scrollback file is written")
        check(not any("older-p" in s for s in sb), "older lines aren't saved to it")
    finally:
        screen = t.text()
        t.stop()
        ircd.terminate()

    if fails:
        print("--- screen\n" + screen)
        sys.exit(1)
    print("tui-history: ok")


if __name__ == "__main__":
    main()

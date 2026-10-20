#!/usr/bin/env python3
"""The TUI's Channel List: /list opens it, typing filters, clicking the
Channel heading sorts by name, a double-click joins.

    tui-chanlist.py NEXCHAT_TUI
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
    ircd = subprocess.Popen([sys.executable, os.path.join(smoke.HERE, "fake_ircd.py"), "--log", log,
                             "--portfile", portfile, "--demo"])
    for _ in range(50):
        if os.path.exists(portfile) and open(portfile).read().strip():
            break
        time.sleep(0.1)
    port = open(portfile).read().strip()
    cfg = os.path.join(tmp, "cfg")
    os.makedirs(cfg)
    open(os.path.join(cfg, "hexchat.conf"), "w").write("net_throttle = 0\nirc_nick1 = lister\ngui_slist_skip = 1\n")
    os.environ.pop("NO_COLOR", None)
    t = smoke.Tui([binary, "-n", "-d", cfg, f"irc://127.0.0.1:-{port}/"], {"TERM": "xterm-256color"})
    fails = []

    def check(cond, what):
        if not cond:
            fails.append(what)
            print("FAIL:", what)

    def rows_order():
        return [c for c in ("#linux", "#help", "#nexchat", "#offtopic") if t.find(c)]

    try:
        t.pump(2.0)
        t.type("/list\r", 1.0)
        check(t.wait_for("Channel List", 2), "/list opens the Channel List")
        check(t.wait_for("#linux", 2), "it fills with the server's channels")
        ys = [t.find(c)[1] for c in ("#linux", "#help", "#nexchat") if t.find(c)]
        check(ys == sorted(ys), "sorted by users, most first")

        # click "Channel" in the header: by name
        p = t.find("Channel   ")
        if p:
            t.click(p[0] + 1, p[1])
            t.pump(0.4)
        ys = [t.find(c)[1] for c in ("#help", "#linux", "#nexchat", "#offtopic") if t.find(c)]
        check(len(ys) == 4 and ys == sorted(ys), "clicking the Channel heading sorts by name")

        # type to filter
        t.type("lin", 0.5)
        check(t.find("#linux") and not t.find("#help"), "typing filters the list")
        check("1 of 4" in t.text(), "the count follows the filter")

        # double-click joins
        p = t.find("#linux")
        if p:
            t.dclick(p[0] + 1, p[1])
        t.pump(1.0)
        check("JOIN #linux" in open(log).read(), "double-click joins the channel")

        t.send(b"\x1b", 0.5)
        check(t.find("Channel List") is None, "Esc closes it")
        # /LIST output goes back to the server tab once it's closed
    finally:
        screen = t.text()
        t.stop()
        ircd.terminate()

    if fails:
        print("--- screen\n" + screen)
        print("--- server.log\n" + open(log).read()[-1500:])
        sys.exit(1)
    print("tui-chanlist: ok")


if __name__ == "__main__":
    main()

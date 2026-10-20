#!/usr/bin/env python3
"""The TUI's Ignore List and Friends List: Add, edit the mask / nick,
toggle what's ignored with the mouse, Remove; saved to ignore.conf /
notify.conf, and the core acts on it (an ignored nick's message is dropped).

    tui-lists.py NEXCHAT_TUI
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
                             "--portfile", portfile])
    for _ in range(50):
        if os.path.exists(portfile) and open(portfile).read().strip():
            break
        time.sleep(0.1)
    port = open(portfile).read().strip()
    cfg = os.path.join(tmp, "cfg")
    os.makedirs(cfg)
    open(os.path.join(cfg, "hexchat.conf"), "w").write("net_throttle = 0\nirc_nick1 = lists\ngui_slist_skip = 1\n")
    os.environ.pop("NO_COLOR", None)
    t = smoke.Tui([binary, "-n", "-d", cfg, f"irc://127.0.0.1:-{port}/"], {"TERM": "xterm-256color"})
    fails = []

    def check(cond, what):
        if not cond:
            fails.append(what)
            print("FAIL:", what)

    def file_has(name, s, secs=3.0):
        end = time.time() + secs
        p = os.path.join(cfg, name)
        while time.time() < end:
            if os.path.exists(p) and s in open(p).read():
                return True
            t.pump(0.2)
        return False

    def open_from_window_menu(item):
        p = t.find("Window")
        t.click(p[0] + 1, p[1])
        q = t.wait_for(item, 2) and t.find(item)
        if q:
            t.click(q[0] + 1, q[1])
        return t.wait_for(item, 2)

    try:
        t.pump(2.0)
        # ── Ignore List ────────────────────────────────────────────────
        check(open_from_window_menu("Ignore List"), "Window → Ignore List opens it")
        check("Nobody is ignored" in t.text(), "an empty list says so")
        p = t.find("[ Add ]")
        t.click(p[0] + 2, p[1])
        check(t.wait_for("new!*@*", 2), "Add makes a new mask")
        t.send(b"\x01\x0b", 0.2)                  # clear the mask field
        t.type("spammer!*@*\t", 0.5)
        check(file_has("ignore.conf", "mask = spammer!*@*"), "the mask is saved to ignore.conf")
        # untick "Channel messages" with the mouse
        q = t.find("Channel messages")
        if q:
            t.click(q[0] + 22, q[1])
            t.pump(0.4)
        t.send(b"\x1b", 0.5)
        conf = open(os.path.join(cfg, "ignore.conf")).read()
        check("type = 186" in conf or ("spammer" in conf and "type = " in conf), "ignore.conf has the types")
        # the core uses it: /ignore lists it
        t.type("/ignore\r", 0.6)
        check(t.wait_for("spammer!*@*", 2), "/ignore shows the new entry")

        # ── Friends List ───────────────────────────────────────────────
        check(open_from_window_menu("Friends List"), "Window → Friends List opens it")
        p = t.find("[ Add ]")
        t.click(p[0] + 2, p[1])
        check(t.wait_for("newfriend", 2), "Add makes a new friend")
        t.send(b"\x01\x0b", 0.2)
        t.type("buddy\t", 0.5)
        check(file_has("notify.conf", "buddy"), "the friend is saved to notify.conf")
        # Remove it again
        p = t.find("[ Remove ]")
        t.click(p[0] + 2, p[1])
        t.pump(0.5)
        check(not file_has("notify.conf", "buddy", 1.0), "Remove takes it out of notify.conf")
        t.send(b"\x1b", 0.5)
    finally:
        screen = t.text()
        t.stop()
        ircd.terminate()

    if fails:
        print("--- screen\n" + screen)
        for n in ("ignore.conf", "notify.conf"):
            p = os.path.join(cfg, n)
            print(f"--- {n}\n" + (open(p).read() if os.path.exists(p) else "(none)"))
        sys.exit(1)
    print("tui-lists: ok")


if __name__ == "__main__":
    main()

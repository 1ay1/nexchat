#!/usr/bin/env python3
"""The TUI's Network List against fake_ircd: it opens at startup (no URL,
gui_slist_skip off), a network is added and edited with keys AND mouse,
saved to servlist.conf, and Connect logs in to the server.

    tui-netlist.py NEXCHAT_TUI
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
    open(os.path.join(cfg, "hexchat.conf"), "w").write(
        "net_throttle = 0\nirc_nick1 = netlister\ngui_slist_skip = 0\n")
    # one network to begin with, so the list isn't the 80 defaults
    open(os.path.join(cfg, "servlist.conf"), "w").write(
        "v=2.16.2\n\nN=Existing\nE=UTF-8 (Unicode)\nF=19\nD=0\nS=irc.example.org/6667\n\n")

    os.environ.pop("NO_COLOR", None)
    t = smoke.Tui([binary, "-n", "-d", cfg], {"TERM": "xterm-256color"})
    fails = []

    def check(cond, what):
        if not cond:
            fails.append(what)
            print("FAIL:", what)

    try:
        t.pump(1.5)
        check(t.wait_for("Network List", 3), "the Network List opens at startup")
        check("Existing" in t.text(), "it lists servlist.conf's networks")

        # Add (mouse: click the button), then type its name over "New Network"
        p = t.find("[ Add ]")
        check(p is not None, "the Add button is drawn")
        if p:
            t.click(p[0] + 2, p[1])
        check(t.wait_for("New Network", 2), "Add makes a New Network")
        # the form has focus on the Network field: replace its text
        t.send(b"\x01\x0b", 0.2)                 # Ctrl-A, Ctrl-K: clear the field
        t.type("Fake\t", 0.4)                    # name, Tab to Servers
        # Servers: select the placeholder "newserver/6667", Enter edits it
        t.send(b"\r", 0.3)
        t.send(b"\x01\x0b", 0.2)
        t.type(f"127.0.0.1/{port}\r", 0.4)
        check(f"127.0.0.1/{port}" in t.text(), "the server entry is edited in place")
        # Autojoin: Tab down to it, type a channel on the "+ add…" row
        t.send(b"\x1b[B", 0.2)                   # ↓ past the server list's + add…
        t.send(b"\x1b[B", 0.2)
        t.type("#joined\r", 0.4)
        check("#joined" in t.text(), "an autojoin channel is added")

        # SSL off for the fake server: click its toggle
        p = t.find("Use SSL for all")
        if p:
            t.click(p[0] + 25, p[1])
            t.pump(0.3)
        # the list on the left now says Fake
        check(t.find("Fake") is not None, "the list shows the renamed network")

        # Connect (click)
        p = t.find("[ Connect ]")
        if p:
            t.click(p[0] + 2, p[1])
        t.pump(3.0)
        check(t.find("Network List") is None, "Connect closes the list")
        log_text = open(log).read() if os.path.exists(log) else ""
        check("NICK netlister" in log_text, "Connect logged in to the network's server")
        check("JOIN #joined" in log_text, "the autojoin channel was joined")

        # it was saved
        conf = open(os.path.join(cfg, "servlist.conf")).read()
        check("N=Fake" in conf and f"S=127.0.0.1/{port}" in conf and "J=#joined" in conf,
              "servlist.conf has the new network")

        # Ctrl-S opens it again, Esc closes it
        t.send(b"\x13", 0.5)
        check(t.wait_for("Network List", 2), "Ctrl-S opens the Network List")
        t.send(b"\x1b", 0.5)
        check(t.find("Network List") is None, "Esc closes it")
    finally:
        screen = t.text()
        t.stop()
        ircd.terminate()

    if fails:
        print("--- screen\n" + screen)
        print("--- server.log\n" + (open(log).read()[-2000:] if os.path.exists(log) else ""))
        print("--- servlist.conf\n" + open(os.path.join(cfg, "servlist.conf")).read()[:1500])
        sys.exit(1)
    print("tui-netlist: ok")


if __name__ == "__main__":
    main()

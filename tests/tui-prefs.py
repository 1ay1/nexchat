#!/usr/bin/env python3
"""The TUI's Preferences: settings apply live (timestamps go from the
chat as soon as they're switched off) and are saved to hexchat.conf.

    tui-prefs.py NEXCHAT_TUI
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
                             "--portfile", portfile])
    for _ in range(50):
        if os.path.exists(portfile) and open(portfile).read().strip():
            break
        time.sleep(0.1)
    port = open(portfile).read().strip()
    cfg = os.path.join(tmp, "cfg")
    os.makedirs(cfg)
    open(os.path.join(cfg, "hexchat.conf"), "w").write(
        "net_throttle = 0\nirc_nick1 = prefer\ngui_slist_skip = 1\nstamp_text = 1\nstamp_text_format = [%H:%M]\n")
    open(os.path.join(cfg, "tui.conf"), "w").write("[tui]\nstamp_repeat=1\n")   # a time on every line
    os.environ.pop("NO_COLOR", None)
    # the pref accessors (by name) must read the fields the core does
    t = smoke.Tui([binary, "-n", "-d", cfg, f"irc://127.0.0.1:-{port}/"],
                  {"TERM": "xterm-256color", "NX_TUI_SELFTEST": "1"})
    fails = []

    def check(cond, what):
        if not cond:
            fails.append(what)
            print("FAIL:", what)

    stamp = re.compile(r"\[\d\d:\d\d\]")
    try:
        t.pump(2.0)
        check(t.alive, "the pref accessors agree with the core (NX_TUI_SELFTEST)")
        t.type("/echo marker-line\r", 0.6)
        line = next((l for l in t.text().splitlines() if "marker-line" in l), "")
        check(stamp.search(line), "timestamps show to begin with")

        # nexchat menu → Preferences…, mouse
        p = t.find("nexchat")
        t.click(p[0] + 1, p[1])
        q = t.wait_for("Preferences", 2) and t.find("Preferences…")
        check(q, "the nexchat menu has Preferences…")
        if q:
            t.click(q[0] + 2, q[1])
        check(t.wait_for("Settings", 2), "Preferences opens")

        # click the "Enable timestamps" toggle
        q = t.find("Enable timestamps")
        check(q is not None, "Appearance shows Enable timestamps")
        if q:
            t.click(q[0] + 30, q[1])
            t.pump(0.4)
        # the chat behind already has no stamps (live), visible once closed
        t.send(b"\x1b", 0.6)
        line = next((l for l in t.text().splitlines() if "marker-line" in l), "")
        check(line and not stamp.search(line), "the chat drops timestamps right away")

        def conf_has(pat, secs=3.0):
            end = time.time() + secs
            while time.time() < end:
                if re.search(pat, open(os.path.join(cfg, "hexchat.conf")).read(), re.M):
                    return True
                t.pump(0.2)
            return False

        check(conf_has(r"^stamp_text = 0$"), "hexchat.conf has stamp_text = 0")

        # a text pref: the user list double-click command, typed into the form
        t.click(p[0] + 1, p[1])
        q = t.wait_for("Preferences", 2) and t.find("Preferences…")
        if q:
            t.click(q[0] + 2, q[1])
        q = t.find("User list")
        if q:
            t.click(q[0] + 1, q[1])
        q = t.find("Execute command")
        check(q is not None, "User list shows the double-click command")
        if q:
            t.click(q[0] + 45, q[1])
            t.send(b"\x01\x0b", 0.2)
            t.type("WHOIS %s\r", 0.4)
        t.send(b"\x1b", 0.6)
        check(conf_has(r"^gui_ulist_doubleclick = WHOIS %s$"), "a text pref is saved")
    finally:
        screen = t.text()
        t.stop()
        ircd.terminate()

    if fails:
        print("--- screen\n" + screen)
        sys.exit(1)
    print("tui-prefs: ok")


if __name__ == "__main__":
    main()

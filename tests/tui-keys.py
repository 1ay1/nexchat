#!/usr/bin/env python3
"""The TUI and the GUI's keybindings.conf (fe-gtk/fkeys.c), against
fake_ircd --demo:

  * no file: the TUI's own keys (the GUI's defaults), and the default
    Check For Replace: "teh " becomes "the " (replace.conf)
  * a file: its bindings run, the GUI's actions with its D1/D2 meaning:
    Run Command (with \\n), Insert in Buffer, Set Buffer, Change Page
    (absolute, Relative, auto), Scroll Page, Last Command; a binding
    takes the key over the TUI's own (Page_Up here runs a command)

    tui-keys.py NEXCHAT_TUI
"""
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from importlib import import_module
smoke = import_module("tui-smoke")

KEYS = """# HexChat key bindings config file

ACCEL=F5
Run Command
D1:/echo f5-ran\\n/echo second-line
D2!

ACCEL=<Alt>q
Insert in Buffer
D1:inserted
D2!

ACCEL=<Primary>y
Set Buffer
D1:/msg nobody hi
D2!

ACCEL=<Alt>1
Change Page
D1:1
D2!

ACCEL=<Primary>Page_Down
Change Page
D1:1
D2:Relative

ACCEL=<Alt>grave
Change Page
D1:auto
D2!

ACCEL=Page_Up
Run Command
D1:/echo pageup-bound
D2!

ACCEL=Up
Last Command
D1!
D2!

ACCEL=space
Check For Replace
D1!
D2!

ACCEL=Return
Check For Replace
D1!
D2!

ACCEL=<Hyper>x
Run Command
D1:/echo never
D2!
"""


def run(binary, with_file):
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
    open(os.path.join(cfg, "hexchat.conf"), "w").write("net_throttle = 0\nirc_nick1 = tester\ngui_slist_skip = 1\n")
    open(os.path.join(cfg, "replace.conf"), "w").write("NAME teh\nCMD the\n\nNAME brb\nCMD be right back\n\n")
    if with_file:
        open(os.path.join(cfg, "keybindings.conf"), "w").write(KEYS)
    os.environ.pop("NO_COLOR", None)
    t = smoke.Tui([binary, "-n", "-d", cfg, f"irc://127.0.0.1:-{port}/#demo"], {"TERM": "xterm-256color"})
    return t, ircd, log


def main():
    binary = os.path.abspath(sys.argv[1])
    fails = []

    def check(cond, what):
        print(("ok:  " if cond else "FAIL:"), what)
        if not cond:
            fails.append(what)

    def input_line(t):
        return t.input_line()

    def to_demo(t):
        p = t.find("#demo")
        if p:
            t.click(p[0] + 1, p[1])
            t.pump(0.3)

    # ── no keybindings.conf ────────────────────────────────────────────
    t, ircd, log = run(binary, False)
    try:
        t.pump(2.0)
        check(t.wait_for("#demo", 5), "no file: in #demo")
        to_demo(t)
        t.type("teh ", 0.3)
        check(input_line(t).rstrip().endswith("the"), "no file: space checks replace.conf (teh → the)")
        t.send(b"\x1bu", 0.2)
        t.type("ok brb\r", 0.6)
        check("ok be right back" in open(log).read(), "no file: Return does too, before sending")
        t.send(b"\x1b[5~", 0.4)                                # PageUp: the TUI's own scroll
        check("pageup-bound" not in t.text(), "no file: PageUp is the TUI's own")

        # the leader: Ctrl-X then a key is that key with Alt (for when tmux
        # keeps the Alt keys)
        t.send(b"\x1b", 0.2)
        t.send(b"\x18", 0.3)
        check("Ctrl-X …" in t.status_line(), "Ctrl-X says it's waiting for a key")
        t.send(b"1", 0.4)
        check("Message Fake" in input_line(t) or "/command" in input_line(t), "Ctrl-X 1 is Alt-1 (the first tab)")
        check("1" not in input_line(t).replace("poke", ""), "...and the 1 isn't typed")
        t.send(b"\x18", 0.2)
        t.send(b"\x1b[C", 0.4)                                 # → after the leader: Alt-→
        check("Message #demo" in input_line(t), "Ctrl-X → is Alt-→ (the next tab)")
        t.send(b"\x18", 0.2)
        t.send(b"\x1b", 0.3)
        t.type("z", 0.3)
        check(input_line(t).rstrip().endswith("z"), "Ctrl-X Esc: never mind, the next key types")
    finally:
        t.stop()
        ircd.terminate()

    # ── a keybindings.conf ─────────────────────────────────────────────
    t, ircd, log = run(binary, True)
    try:
        t.pump(2.0)
        check(t.wait_for("#demo", 5), "with a file: in #demo")
        to_demo(t)
        t.send(b"\x1b[15~", 0.6)                               # F5
        check(t.find("f5-ran") and t.find("second-line"), "Run Command: both lines of D1 (\\n) run")
        t.send(b"\x1bq", 0.3)                                  # Alt-Q
        check(input_line(t).rstrip().endswith("inserted"), "Insert in Buffer puts D1 at the cursor")
        t.send(b"\x19", 0.3)                                   # Ctrl-Y
        check(input_line(t).rstrip().endswith("/msg nobody hi"), "Set Buffer replaces the line with D1")
        t.send(b"\x1bu", 0.2)
        t.type("teh ", 0.3)
        check(input_line(t).rstrip().endswith("the"), "the file's space binding: Check For Replace")
        t.send(b"\x1bu", 0.2)
        t.type("/echo history-one\r", 0.5)
        t.send(b"\x1b[A", 0.3)                                 # Up: Last Command
        check("/echo history-one" in input_line(t), "Last Command brings the last line back")
        t.send(b"\x1bu", 0.2)
        t.send(b"\x1b[5~", 0.5)                                # Page_Up bound to a command
        check(t.find("pageup-bound") is not None, "a binding takes a key over the TUI's own (Page_Up)")
        t.send(b"\x1b1", 0.4)                                  # Alt-1: page 1, the server tab
        check("Welcome to the fake network" in t.text() or "MOTD" in t.text(), "Change Page 1: the first tab")
        t.send(b"\x1b[6;5~", 0.4)                              # Ctrl-PageDown: relative +1
        check(t.find("alice │") is not None or "#demo" in t.text().splitlines()[1], "Change Page Relative 1: the next")
        # someone messages us in another tab: Alt-` goes there (lastact)
        t.send(b"\x1b1", 0.4)
        t.send(b"\x1b`", 0.5)
        check(t.find("hi, this is a query") is not None, "Change Page auto: the tab with the newest activity (heidi)")
    finally:
        screen = t.text()
        t.stop()
        ircd.terminate()

    if fails:
        print("--- screen\n" + screen)
        sys.exit(1)
    print("tui-keys: ok")


if __name__ == "__main__":
    main()

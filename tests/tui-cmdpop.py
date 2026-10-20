#!/usr/bin/env python3
"""The TUI's command popup (fe-gtk/cmdpopup.c), against fake_ircd --demo:

  * "/" opens it: the everyday commands under "Most used", each with what
    to type after it and what it does, and the selected one's details
  * typing narrows it ("/jo" → /join first); Down/Up move; Tab fills in
  * after the command: its usage on top, the argument under the caret
    stressed, and values for it (/kick ma → the nicks starting "ma")
  * Tab/Enter on a value fills it in; Enter with nothing to pick sends
  * Esc closes it, and it stays closed for that command
  * a click on a row fills it in
  * gui_input_cmdhelp 0: no popup

    tui-cmdpop.py NEXCHAT_TUI
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
    open(os.path.join(cfg, "hexchat.conf"), "w").write(
        "net_throttle = 0\nirc_nick1 = tester\ngui_slist_skip = 1\ngui_input_cmdhelp = 1\n")
    os.environ.pop("NO_COLOR", None)
    t = smoke.Tui([binary, "-n", "-d", cfg, f"irc://127.0.0.1:-{port}/#demo"], {"TERM": "xterm-256color"})
    fails = []

    def check(cond, what):
        print(("ok:  " if cond else "FAIL:"), what)
        if not cond:
            fails.append(what)

    def clear():
        t.send(b"\x1bu", 0.2)              # Alt-U: kill to start
        t.send(b"\x1b", 0.2)

    try:
        t.pump(2.0)
        check(t.wait_for("#demo", 5), "in #demo")
        p = t.find("#demo")
        if p:
            t.click(p[0] + 1, p[1])
            t.pump(0.3)

        t.type("/", 0.5)
        check(t.find("Commands") is not None and t.find("Most used") is not None,
              "\"/\" opens it: Commands, Most used first")
        check(t.find("/join") is not None and t.find("Go into a channel") is not None,
              "each with what it does")
        check(t.find("Tab fill in") is not None, "the keys at the bottom")
        t.type("jo", 0.4)
        check(t.find("Commands matching") is not None, "typing narrows it")
        t.send(b"\t", 0.4)
        check(t.input_line().endswith("/join"), "Tab fills in /join")
        check(t.find("<#channel>") is not None or t.find("<channel>") is not None,
              "then its usage, with the argument to type")
        clear()

        t.type("/kick ma", 0.5)
        check("mallory" in t.text(), "/kick ma: the nicks starting ma")
        check(t.find("Remove someone from the channel") is not None, "and what /kick does")
        t.send(b"\t", 0.4)
        check(t.input_line().rstrip().endswith("/kick mallory"), "Tab fills in the nick")
        clear()

        # Esc: closed for this command
        t.type("/me", 0.4)
        check(t.find("Say what you") is not None, "/me: it's open")
        t.send(b"\x1b", 0.4)
        check(t.find("Say what you") is None, "Esc closes it")
        t.type(" waves", 0.4)
        check(t.find("Say what you") is None, "...and it stays closed for /me")
        t.send(b"\r", 0.6)
        check("PRIVMSG #demo :\x01ACTION waves\x01" in open(log).read(), "Enter sends the line")

        # Enter: on a whole command it sends, on part of one it fills in
        t.type("/jo", 0.4)
        t.send(b"\r", 0.4)
        check(t.input_line().endswith("/join"), "Enter on /jo fills in /join")
        clear()
        t.type("/list", 0.4)
        t.send(b"\r", 0.8)
        check("LIST" in open(log).read(), "Enter on /list (a whole command) sends it")
        t.send(b"\x1b", 0.4)
        clear()
        # with values listed, Enter sends what's typed; after ↓ it fills in
        t.type("/kick mal", 0.4)
        t.send(b"\x1b[B", 0.2)
        t.send(b"\x1b[A", 0.2)
        t.send(b"\r", 0.4)
        check(t.input_line().rstrip().endswith("/kick mallory"), "Enter after picking a value fills it in")
        clear()

        # a click fills in
        t.type("/", 0.5)
        p = None
        for y, line in enumerate(t.screen.display):
            i = line.find(" /msg ")
            if i >= 0 and "│" in line[:i]:
                p = (i + 1, y)
        if p:
            t.click(p[0] + 1, p[1])
            t.pump(0.3)
        check(t.input_line().endswith("/msg"), "a click on a row fills it in")
        clear()

        # off
        t.type("/set gui_input_cmdhelp 0\r", 0.5)
        t.type("/", 0.4)
        check(t.find("Most used") is None, "gui_input_cmdhelp 0: no popup")
        clear()
    finally:
        screen = t.text()
        t.stop()
        ircd.terminate()

    if fails:
        print("--- screen\n" + screen)
        sys.exit(1)
    print("tui-cmdpop: ok")


if __name__ == "__main__":
    main()

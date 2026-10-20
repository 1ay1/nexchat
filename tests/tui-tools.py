#!/usr/bin/env python3
"""The TUI's Window menu tools against fake_ircd --demo, keys and mouse, the
same windows as the GUI's (fe-gtk/urlgrab.c, mentions.c, plugingui.c,
banlist.c):

  * URL Grabber lists the links seen in chat; Clear empties it
  * Mentions lists bob's highlight and heidi's query; Go to switches tab
  * Plugins and Scripts: Load... a real plugin (checksum), Unload it
  * Ban List (from #demo): the server's two bans; mark one, Remove sends
    MODE -b; Crop removes the others

    tui-tools.py NEXCHAT_TUI
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
    plugin = os.path.join(os.path.dirname(binary), "..", "..", "plugins", "checksum")
    plugin = next((os.path.abspath(os.path.join(plugin, f)) for f in sorted(os.listdir(plugin))
                   if f.startswith("checksum.") and f.split(".")[-1] in ("so", "dylib")), None) \
        if os.path.isdir(plugin) else None
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
        "net_throttle = 0\nirc_nick1 = tester\ngui_slist_skip = 1\nurl_grabber = 1\n")
    os.environ.pop("NO_COLOR", None)
    t = smoke.Tui([binary, "-n", "-d", cfg, f"irc://127.0.0.1:-{port}/#demo"], {"TERM": "xterm-256color"})
    fails = []

    def check(cond, what):
        print(("ok:  " if cond else "FAIL:"), what)
        if not cond:
            fails.append(what)

    def menu(item):
        p = t.find("Window")
        t.click(p[0] + 1, p[1])
        q = t.wait_for(item, 2) and t.find(item)
        if q:
            t.click(q[0] + 1, q[1])
        t.pump(0.4)

    def button(label):
        p = t.find(f"[ {label} ]")
        if p:
            t.click(p[0] + 2, p[1])
            t.pump(0.5)
        return p is not None

    def server_got(s, secs=3):
        end = time.time() + secs
        while time.time() < end:
            if s in open(log).read():
                return True
            t.pump(0.2)
        return False

    try:
        t.pump(2.0)
        check(t.wait_for("#demo", 5) and t.wait_for("hexchat.github.io", 5), "in #demo, the chatter arrived")
        # back to #demo: the query from heidi may have taken the front
        p = t.find("#demo")
        if p:
            t.click(p[0] + 1, p[1])
            t.pump(0.3)

        # ── URL Grabber ────────────────────────────────────────────────
        menu("URL Grabber")
        check(t.wait_for("╭─ URL Grabber", 2), "Window → URL Grabber opens it")
        check(t.wait_for("https://hexchat.github.io", 2), "the link erin posted is listed")
        check(t.find("[ Open ]") and t.find("[ Save As... ]"), "Open / Copy / Clear / Save As..., as the GUI")
        button("Clear")
        check(t.wait_for("No URLs yet", 2), "Clear empties it")
        t.send(b"\x1b", 0.4)
        check(t.find("╭─ URL Grabber") is None, "Esc closes it")

        # ── Mentions ───────────────────────────────────────────────────
        menu("Mentions")
        check(t.wait_for("╭─ Mentions", 2), "Window → Mentions opens it")
        check(t.wait_for("did you try the new build", 2), "bob's highlight is listed")
        check(t.wait_for("hi, this is a query", 2), "heidi's private message is listed")
        check("Fake / private" in t.text() and "Fake / #demo" in t.text(), "where: network / channel, or private")
        if os.environ.get("SHOW"):
            print(t.text())
        p = t.find("hi, this is a query")
        if p:
            t.dclick(p[0] + 1, p[1])            # double-click: go there
            t.pump(0.6)
        check(t.find("╭─ Mentions") is None, "double-click closes it...")
        check(t.find("heidi │ hi, this is a query") and not t.find("did you try the new build"),
              "...and goes to heidi's tab")
        p = t.find("#demo")
        if p:
            t.click(p[0] + 1, p[1])
            t.pump(0.3)

        # ── Plugins and Scripts ────────────────────────────────────────
        menu("Plugins and Scripts")
        check(t.wait_for("╭─ Plugins and Scripts", 2), "Window → Plugins and Scripts opens it")
        check(t.find("[ Load... ]") and t.find("[ Unload ]") and t.find("[ Reload ]"), "Load... / Unload / Reload")
        if plugin:
            button("Load...")
            check(t.wait_for("Select a Plugin or Script to load", 2), "Load... asks for a file (in addons/)")
            t.send(b"\x15", 0.2)               # ^U: clear the suggested dir
            t.type(plugin + "\r", 1.0)
            check(t.wait_for("Checksum", 3), "the plugin loaded shows up in the list")
            p = t.find("Checksum")
            if p:
                t.click(p[0] + 1, p[1])
            button("Unload")
            check(t.wait_for("No plugins or scripts loaded", 3), "Unload takes it out")
        t.send(b"\x1b", 0.4)

        # ── Ban List ───────────────────────────────────────────────────
        menu("Ban List")
        check(t.wait_for("╭─ Ban List — #demo", 2), "Window → Ban List opens it for #demo")
        check(server_got("mode #demo +b"), "it asks the server for +b")
        check(t.wait_for("*!*@spam.example", 3) and t.wait_for("troll!*@*", 3), "the two bans are listed")
        check("◉ Bans" in t.text(), "a Bans checkbox, ticked")
        p = t.find("troll!*@*")
        if p:
            t.click(p[0] + 1, p[1])
        t.send(b" ", 0.3)                       # mark it
        check("1 marked" in t.text(), "Space marks the row")
        if os.environ.get("SHOW"):
            print(t.text())
        button("Remove")
        check(server_got("MODE #demo -b troll!*@*"), "Remove sends MODE -b for the marked one")
        check(t.wait_for("2 in the list", 3), "and it lists them again")
        p = t.find("troll!*@*")
        if p:
            t.click(p[0] + 1, p[1])
        button("Crop")
        check(server_got("MODE #demo -b *!*@spam.example"), "Crop removes the others")
        t.wait_for("2 in the list", 3)
        button("Clear")
        check(t.wait_for("Are you sure", 2), "Clear asks first")
        t.send(b"\x1b", 0.4)
        t.send(b"\x1b", 0.4)
        check(t.find("╭─ Ban List") is None, "Esc closes it")

        # ── the nick menu: Ignore, then it says Unignore ───────────────
        def nick_menu(item):
            p = t.find("ivan")
            if not p:
                return False
            t.click(p[0] + 1, p[1], button=2)
            if not t.wait_for("Open Dialog Window", 2):
                return False
            if item == "Ignore" and t.find("Unignore"):
                t.send(b"\x1b", 0.3)
                return False
            q = t.find(item)
            if q:
                t.click(q[0] + 1, q[1])
                t.pump(0.6)
            else:
                t.send(b"\x1b", 0.3)
            return q is not None
        p = t.find("#demo")
        if p:
            t.click(p[0] + 1, p[1])
            t.pump(0.3)
        check(nick_menu("Ignore"), "the nick menu has Ignore")
        check(nick_menu("Unignore"), "once ignored, the nick menu says Unignore")
        check(nick_menu("Ignore"), "Unignore undoes it: it's Ignore again")
        nick_menu("Unignore")

        # outside a channel: the GUI's message
        p = t.find("heidi")
        if p:
            t.click(p[0] + 1, p[1])
            t.pump(0.3)
        menu("Ban List")
        check(t.wait_for("You can only open the Ban List", 2), "outside a channel: says so")
        t.send(b"\r", 0.4)
    finally:
        screen = t.text()
        t.stop()
        ircd.terminate()

    if fails:
        print("--- screen\n" + screen)
        print("--- server.log\n" + open(log).read()[-1500:])
        sys.exit(1)
    print("tui-tools: ok")


if __name__ == "__main__":
    main()

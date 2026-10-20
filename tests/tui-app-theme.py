#!/usr/bin/env python3
"""Every window of the real app (nexchat-tui) under a light and a dark maya
scheme and native: no glyph in the terminal's own colours, no reverse video
on a scheme, nothing unreadable. tui-theme-check.py does the gallery; this
does nexchat-tui's windows over a live connection.

    tui-app-theme.py NEXCHAT_TUI
"""
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from importlib import import_module
smoke = import_module("tui-smoke")
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "ux"))
check_mod = import_module("tui-theme-check")

NAMED = {"black", "red", "green", "brown", "blue", "magenta", "cyan", "white", "brightblack", "brightred",
         "brightgreen", "brightbrown", "brightblue", "brightmagenta", "brightcyan", "brightwhite", "default"}


def cells_bad(screen, theme):
    bad = []
    for y in range(smoke.ROWS):
        for x in range(smoke.COLS):
            c = screen.buffer[y][x]
            if theme == "native":
                if c.fg not in NAMED or c.bg not in NAMED:
                    bad.append(f"{x},{y} {c.data!r} fg={c.fg} bg={c.bg}")
                continue
            if not c.data.strip():
                continue
            if c.fg == "default" or c.bg == "default":
                bad.append(f"{x},{y} {c.data!r}: the terminal's own colour (fg={c.fg} bg={c.bg})")
            elif c.reverse:
                bad.append(f"{x},{y} {c.data!r}: reverse video")
            elif c.fg == c.bg:
                bad.append(f"{x},{y} {c.data!r}: fg == bg")
            elif c.data not in check_mod.LINES and check_mod.contrast(c.fg, c.bg) < 1.5:
                bad.append(f"{x},{y} {c.data!r}: {c.fg} on {c.bg}")
    return bad


def main():
    binary = os.path.abspath(sys.argv[1])
    fails = 0
    for theme in ("native", "Atom One Light", "Dracula"):
        tmp = tempfile.mkdtemp()
        log, portfile = os.path.join(tmp, "s.log"), os.path.join(tmp, "port")
        ircd = subprocess.Popen([sys.executable, os.path.join(smoke.HERE, "fake_ircd.py"), "--log", log,
                                 "--portfile", portfile, "--demo"])
        while not (os.path.exists(portfile) and open(portfile).read().strip()):
            time.sleep(0.05)
        port = open(portfile).read().strip()
        cfg = os.path.join(tmp, "cfg")
        os.makedirs(cfg)
        open(os.path.join(cfg, "hexchat.conf"), "w").write("net_throttle = 0\nirc_nick1 = themer\ngui_slist_skip = 1\n")
        open(os.path.join(cfg, "tui.conf"), "w").write(f"[tui]\ntheme={theme}\n")
        os.environ.pop("NO_COLOR", None)
        t = smoke.Tui([binary, "-n", "-d", cfg, f"irc://127.0.0.1:-{port}/"],
                      {"TERM": "xterm-256color", "COLORTERM": "truecolor"})
        t.pump(1.5)
        t.type("/join #nexchat\r", 2.5)
        screens = [("main window", [], "#nexchat")]

        def window(item):
            def pick():
                p = t.find(item)
                if p:
                    t.click(p[0] + 1, p[1], t=0.6)
            return [lambda: t.click(t.find("Window")[0] + 1, 0, t=0.4), pick]
        screens += [("nick menu", [lambda: t.click(110, 6, button=2)], "Insert in Input"),
                    ("View menu", [lambda: t.send(b"\x1b", 0.3), lambda: t.click(12, 0)], "Timestamps"),
                    ("Network List", [lambda: t.send(b"\x1b", 0.3), lambda: t.send(b"\x13", 0.6)], "Network List"),
                    ("Preferences", [lambda: t.send(b"\x1b", 0.3), lambda: t.click(3, 0),
                                     lambda: t.click(5, 3)], "Settings"),
                    ("Channel List", [lambda: t.send(b"\x1b", 0.3), lambda: t.type("/list\r", 1.0)], "#linux"),
                    ("Ban List", [lambda: t.send(b"\x1b", 0.3)] + window("Ban List"),
                     "[ Refresh ]"),
                    ("Mentions", [lambda: t.send(b"\x1b", 0.3)] + window("Mentions"), "[ Go to ]"),
                    ("find bar", [lambda: t.send(b"\x1b", 0.3), lambda: t.send(b"\x06", 0.3),
                                   lambda: t.type("nexchat", 0.6)], " of "),
                    ("prompt", [lambda: t.send(b"\x1b", 0.3), lambda: t.type('/getstr X "echo" "A question?"\r', 0.6)],
                     "A question?")]
        for name, steps, marker in screens:
            for s in steps:
                s()
                t.pump(0.3)
            t.pump(0.4)
            if marker not in t.text():
                fails += 1
                print(f"{theme} / {name}: not on screen (looked for {marker!r})")
                continue
            bad = cells_bad(t.screen, theme)
            if bad:
                fails += 1
                print(f"{theme} / {name}: {len(bad)} cell(s)")
                for b in bad[:5]:
                    print("   ", b)
        t.stop()
        ircd.terminate()

        # the tab strip (gui_tab_layout 0) under the same theme
        cfg2 = os.path.join(tmp, "cfg2")
        os.makedirs(cfg2)
        open(os.path.join(cfg2, "hexchat.conf"), "w").write(
            "net_throttle = 0\nirc_nick1 = themer\ngui_slist_skip = 1\ngui_tab_layout = 0\ngui_tab_pos = 6\n")
        open(os.path.join(cfg2, "tui.conf"), "w").write(f"[tui]\ntheme={theme}\n")
        ircd = subprocess.Popen([sys.executable, os.path.join(smoke.HERE, "fake_ircd.py"), "--log", log + "2",
                                 "--portfile", portfile + "2", "--demo"])
        for _ in range(100):
            if os.path.exists(portfile + "2") and open(portfile + "2").read().strip():
                break
            time.sleep(0.05)
        port2 = open(portfile + "2").read().strip()
        t = smoke.Tui([binary, "-n", "-d", cfg2, f"irc://127.0.0.1:-{port2}/#demo"],
                      {"TERM": "xterm-256color", "COLORTERM": "truecolor"})
        t.pump(2.5)
        if "heidi" not in t.text() or "#demo" not in t.text():
            fails += 1
            print(f"{theme} / tab strip: not on screen")
        else:
            bad = cells_bad(t.screen, theme)
            if bad:
                fails += 1
                print(f"{theme} / tab strip: {len(bad)} cell(s)")
                for b in bad[:5]:
                    print("   ", b)
        t.stop()
        ircd.terminate()
    if fails:
        sys.exit(1)
    print("tui-app-theme: ok")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Poke nexchat-tui by hand: tui-poke.py BIN -- step... ; each step is
keys (with <esc> <enter> <tab> <c-x> <a-x>), click:X,Y[,button], wheel:X,Y,up|down,
sleep:S, or show (print the screen). Against fake_ircd --demo."""
import os, sys, time, tempfile, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from importlib import import_module
smoke = import_module("tui-smoke")

KEYS = {"<esc>": "\x1b", "<enter>": "\r", "<tab>": "\t", "<up>": "\x1b[A", "<down>": "\x1b[B",
        "<right>": "\x1b[C", "<left>": "\x1b[D",
        "<f10>": "\x1b[21~", "<f2>": "\x1bOQ", "<f3>": "\x1bOR", "<pgup>": "\x1b[5~"}


def main():
    binary = os.path.abspath(sys.argv[1])
    steps = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    tmp = tempfile.mkdtemp()
    log, portfile = os.path.join(tmp, "s.log"), os.path.join(tmp, "port")
    ircd = subprocess.Popen([sys.executable, os.path.join(smoke.HERE, "fake_ircd.py"), "--log", log,
                             "--portfile", portfile, "--demo"])
    while not (os.path.exists(portfile) and open(portfile).read().strip()):
        time.sleep(0.05)
    port = open(portfile).read().strip()
    cfg = os.path.join(tmp, "cfg"); os.makedirs(cfg)
    open(os.path.join(cfg, "hexchat.conf"), "w").write("net_throttle = 0\nirc_nick1 = poke\n")
    os.environ.pop("NO_COLOR", None)
    t = smoke.Tui([binary, "-n", "-d", cfg, f"irc://127.0.0.1:-{port}/"], {"TERM": "xterm-256color"})
    t.pump(1.5)
    for s in steps:
        if s == "show":
            print(t.text()); print("-" * smoke.COLS)
        elif s.startswith("sleep:"):
            t.pump(float(s[6:]))
        elif s.startswith("click:"):
            a = s[6:].split(","); t.click(int(a[0]), int(a[1]), int(a[2]) if len(a) > 2 else 0)
        elif s.startswith("wheel:"):
            a = s[6:].split(","); t.wheel(int(a[0]), int(a[1]), a[2] == "up")
        elif s.startswith("<c-") and len(s) == 5:
            t.send(chr(ord(s[3]) - 96), 0.3)
        elif s.startswith("<a-") and len(s) == 5:
            t.send("\x1b" + s[3], 0.3)
        elif s in KEYS:
            t.send(KEYS[s], 0.4)
        else:
            t.send(s, 0.4)
    t.stop(); ircd.terminate()
    if "--log" in sys.argv:
        print(open(log).read())


if __name__ == "__main__":
    main()

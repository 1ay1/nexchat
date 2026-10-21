#!/usr/bin/env python3
"""Random keys, clicks, wheel, pastes and resizes at nexchat-tui, with
popups opening and closing all the time (fuzz, seeded, so a failure can be
run again). After every step it must still be alive; every few steps the
screen is checked: nothing past the width, every box that starts on screen
also ends on it, and the input frame is at the bottom. At the end Ctrl-Q
must still quit it.

    tui-fuzz.py NEXCHAT_TUI [STEPS] [SEED]      (TUI_FUZZ_SEED overrides)

On a failure it prints the seed and the last steps. To dig in:
TUI_FUZZ_FROM=N checks the screen after every step from N on,
TUI_FUZZ_RAW=FILE / TUI_FUZZ_ERR=FILE keep the tty output / stderr,
TUI_FUZZ_PROC=FILE notes where the process sits when the screen goes
blank, TUI_FUZZ_PROF=1 turns on MAYA_FRAME_PROF.
"""
import os
import random
import signal
import struct
import fcntl
import termios
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from importlib import import_module
smoke = import_module("tui-smoke")

# what opens something, and other keys that do things
OPENERS = [b"\x10", b"\x07", b"\x1bOP", b"\x13", b"/", b"\x1b[21~", b"\x1br", b"\x1bj",
           b"\x10bouncer\r", b"\x10preferences\r", b"\x10channel list\r", b"\x10transfers\r",
           b"\x10ignore\r", b"\x10url grabber\r", b"\x10mentions\r", b"\x10plugins\r",
           b"\x10about\r", b"\x10join\r", b"\x10theme\r", b"\x06"]
KEYS = [b"\x1b", b"\r", b"\t", b"\x1b[Z", b"\x1b[A", b"\x1b[B", b"\x1b[C", b"\x1b[D",
        b"\x1b[5~", b"\x1b[6~", b"\x1b[H", b"\x1b[F", b"\x7f", b" ", b"a", b"Z", b"1",
        b"\x1bOQ", b"\x1bOR", b"\x0c", b"\x1b[1;5A", b"\x1b\r", "é".encode(), "漢".encode()]


def main():
    binary = os.path.abspath(sys.argv[1])
    steps = int(sys.argv[2]) if len(sys.argv) > 2 else int(os.environ.get("TUI_FUZZ_STEPS", "400"))
    seed = int(os.environ.get("TUI_FUZZ_SEED", sys.argv[3] if len(sys.argv) > 3 else str(int(time.time()))))
    rnd = random.Random(seed)
    print(f"seed {seed}, {steps} steps")
    tmp = tempfile.mkdtemp()
    log, portfile = os.path.join(tmp, "server.log"), os.path.join(tmp, "port")
    ircd = subprocess.Popen([sys.executable, os.path.join(smoke.HERE, "fake_ircd.py"), "--log", log,
                             "--portfile", portfile, "--demo", "--timeout", "600",
                             "--caps", "batch draft/chathistory server-time message-tags",
                             "--history-pages", "3"])
    for _ in range(50):
        if os.path.exists(portfile) and open(portfile).read().strip():
            break
        time.sleep(0.1)
    port = open(portfile).read().strip()
    cfg = os.path.join(tmp, "cfg")
    os.makedirs(cfg)
    open(os.path.join(cfg, "hexchat.conf"), "w").write(
        "net_throttle = 0\nirc_nick1 = fuzz\ngui_slist_skip = 1\ngui_multiline = 1\n")
    os.environ.pop("NO_COLOR", None)
    for k in ("TMUX", "VTE_VERSION", "TERM_PROGRAM"):
        os.environ.pop(k, None)
    smoke.COLS, smoke.ROWS = 100, 30
    err = os.path.join(tmp, "stderr")
    # no soju for it to find: "Set it up" would start a real one
    fakebin = os.path.join(tmp, "bin")
    os.makedirs(fakebin)
    env_path = ":".join(p for p in os.environ.get("PATH", "").split(":")
                        if p and not os.path.exists(os.path.join(p, "soju")))
    t = smoke.Tui([binary, "-n", "-d", cfg, f"irc://127.0.0.1:-{port}/#demo"],
                  {"TERM": "xterm-256color", "TUI_STDERR": err, "PATH": env_path or fakebin,
                   **({"MAYA_FRAME_PROF": "1"} if os.environ.get("TUI_FUZZ_PROF") else {})})
    raw = bytearray()
    feed = t.stream.feed

    def rec(d):
        raw.extend(d)
        del raw[:-400000]
        feed(d)
    t.stream.feed = rec
    fails = []
    trail = []

    def check(cond, what):
        if not cond:
            print("FAIL:", what)
            fails.append(what)

    def resize(c, r):
        raw.extend(b"\x00RESIZE%dx%d\x00" % (c, r))
        t.screen.resize(r, c)
        fcntl.ioctl(t.fd, termios.TIOCSWINSZ, struct.pack("HHHH", r, c, 0, 0))
        os.kill(t.pid, signal.SIGWINCH)

    def screen_ok(where):
        d = t.screen.display
        cols = t.screen.columns
        blank = not any(l.strip() for l in d)
        if blank:
            print("blank: cursor", t.screen.cursor.x, t.screen.cursor.y, "margins", t.screen.margins,
                  "lines", t.screen.lines, "buffer rows", len(t.screen.buffer), "dirty", len(t.screen.dirty))
            if os.environ.get("TUI_FUZZ_PROC"):
                info = []
                for f in ("wchan", "syscall"):
                    try:
                        info.append(f + ": " + open(f"/proc/{t.pid}/{f}").read().strip())
                    except OSError as e:
                        info.append(f"{f}: {e}")
                open(os.environ["TUI_FUZZ_PROC"], "w").write("\n".join(info))
        check(not blank, f"{where}: the screen is blank (alive={t.alive}, {cols}x{t.screen.lines})")
        for y, line in enumerate(d):
            if len(line) > cols:
                check(False, f"{where}: row {y} wider than the screen")
        for y, line in enumerate(d):
            for x, ch in enumerate(line):
                if ch == "╭" and not any(len(d[y2]) > x and d[y2][x] in "╰├" for y2 in range(y + 1, len(d))):
                    check(False, f"{where}: the box at {x},{y} has no bottom on screen")
                    print("\n".join(l.rstrip() for l in d))
                    return

    try:
        t.pump(2.5)
        check(t.wait_for("#demo", 5), "started, in #demo")
        for i in range(steps):
            r = rnd.random()
            cols, rows = t.screen.columns, t.screen.lines
            if r < 0.22:
                k = rnd.choice(OPENERS)
                what = f"open {k!r}"
                t.send(k, 0.05)
            elif r < 0.62:
                k = rnd.choice(KEYS)
                what = f"key {k!r}"
                t.send(k, 0.03)
            elif r < 0.80:
                x, y = rnd.randrange(cols), rnd.randrange(rows)
                b = rnd.choice([0, 0, 0, 2, 1])
                what = f"click {x},{y} b{b}"
                t.click(x, y, button=b, t=0.03)
            elif r < 0.88:
                x, y = rnd.randrange(cols), rnd.randrange(rows)
                up = rnd.random() < 0.6
                what = f"wheel {x},{y} {'up' if up else 'down'}"
                t.wheel(x, y, up, t=0.03)
            elif r < 0.93:
                txt = rnd.choice(["hello", "line1\nline2", "/", "x" * 300, "https://example.org/(a)", "\t"])
                what = f"paste {txt[:12]!r}"
                t.send(b"\x1b[200~" + txt.encode() + b"\x1b[201~", 0.03)
            else:
                c, rr = rnd.choice([(20, 8), (30, 10), (44, 14), (60, 20), (100, 30), (160, 45), (rnd.randint(20, 180), rnd.randint(8, 50))])
                what = f"resize {c}x{rr}"
                resize(c, rr)
                t.pump(0.15)
            trail.append(what)
            if not t.alive:
                check(False, f"died after step {i}: {what}")
                break
            # keep the line from piling up (Enter sends it; drop the rest)
            if i % 25 == 24:
                t.send(b"\x1b\x1b\x1b", 0.1)
                t.pump(0.3)
            if i % 5 == 4 or i >= int(os.environ.get("TUI_FUZZ_FROM", "1000000")):
                t.pump(0.15)
                screen_ok(f"step {i} ({what})")
                if fails:
                    break
        t.pump(0.5)
        check(t.alive, "still running at the end")
        # back to a sane size, every popup shut, then Ctrl-Q quits
        resize(100, 30)
        t.pump(0.4)
        for _ in range(6):
            t.send(b"\x1b", 0.1)
        # a slow machine can read the last Esc and the ^Q together, as
        # Alt-^Q: let the Escs land first
        t.pump(1.0)
        before_quit = t.text()
        t.send(b"\x11", 0.5)

        def wait_exit(secs):
            end = time.time() + secs
            while time.time() < end:
                try:
                    p, s = os.waitpid(t.pid, os.WNOHANG)
                except ChildProcessError:
                    return t.pid, 0
                if p:
                    return p, s
                t.pump(0.2)
            return 0, 0
        pid, status = wait_exit(4)
        if pid == 0:
            # a quit dialog may ask: say yes
            t.send(b"\r", 0.3)
            pid, status = wait_exit(6)
        if pid == 0:
            # still here: one more ^Q, on its own
            t.send(b"\x11", 0.5)
            pid, status = wait_exit(6)
        check(pid != 0, "Ctrl-Q still quits")
        if pid == 0:
            print("--- before Ctrl-Q\n" + before_quit)
        if pid and os.WIFSIGNALED(status):
            check(False, f"ended by signal {os.WTERMSIG(status)}")
    finally:
        screen = t.text()
        t.stop()
        ircd.terminate()

    if fails:
        print(f"--- seed {seed}; last steps:")
        for w in [x for x in trail if x.startswith("resize")][-3:] + trail[-15:]:
            print("   ", w)
        if os.path.exists(err):
            e = open(err, errors="replace").read()
            if os.environ.get("TUI_FUZZ_ERR"):
                open(os.environ["TUI_FUZZ_ERR"], "w").write(e)
            if e.strip():
                print("--- stderr\n" + e[-3000:])
        print("--- screen\n" + screen)
        if os.environ.get("TUI_FUZZ_RAW"):
            open(os.environ["TUI_FUZZ_RAW"], "wb").write(bytes(raw))
        sys.exit(1)
    print("tui-fuzz: ok")


if __name__ == "__main__":
    main()

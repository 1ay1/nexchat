#!/usr/bin/env python3
"""Drive nexchat-tui in a pty against tests/fake_ircd.py, the way a user
would: keys AND mouse (SGR 1006 reports), and check both what the server
saw and what's on screen (pyte as the terminal).

    tui-smoke.py NEXCHAT_TUI

Exit 0 on success; on failure prints the screen and the server log.
"""
import os
import pty
import select
import signal
import struct
import subprocess
import sys
import tempfile
import time
import fcntl
import termios

import pyte

COLS, ROWS = 120, 32
HERE = os.path.dirname(os.path.abspath(__file__))


class Tui:
    def __init__(self, argv, env):
        self.screen = pyte.Screen(COLS, ROWS)
        self.stream = pyte.ByteStream(self.screen)
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.environ.update(env)
            err = os.environ.get("TUI_STDERR")
            if err:
                os.dup2(os.open(err, os.O_WRONLY | os.O_CREAT | os.O_TRUNC), 2)
            os.execv(argv[0], argv)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
        self.alive = True

    def pump(self, t=0.3):
        end = time.time() + t
        while time.time() < end and self.alive:
            r, _, _ = select.select([self.fd], [], [], max(0, end - time.time()))
            if not r:
                continue
            try:
                data = os.read(self.fd, 65536)
            except OSError:
                self.alive = False
                return
            if not data:
                self.alive = False
                return
            if b"\x1b[5n" in data:          # maya's frame ack probe
                os.write(self.fd, b"\x1b[0n")
            if b"\x1b[6n" in data:
                os.write(self.fd, b"\x1b[1;1R")
            self.stream.feed(data)

    def send(self, s, t=0.3):
        os.write(self.fd, s.encode() if isinstance(s, str) else s)
        self.pump(t)

    def type(self, s, t=0.4):
        self.send(s, t)

    # SGR mouse: CSI < b ; x ; y M (press) / m (release), 1-based
    def click(self, x, y, button=0, t=0.4, release=True):
        self.send(f"\x1b[<{button};{x + 1};{y + 1}M", 0.05)
        if release:
            self.send(f"\x1b[<{button};{x + 1};{y + 1}m", t)

    def dclick(self, x, y, t=0.4):
        self.click(x, y, t=0.03)
        self.click(x, y, t=t)

    def wheel(self, x, y, up=True, t=0.3):
        self.send(f"\x1b[<{64 if up else 65};{x + 1};{y + 1}M", t)

    def drag(self, x0, y0, x1, y1, t=0.4):
        self.send(f"\x1b[<0;{x0 + 1};{y0 + 1}M", 0.05)
        for x in range(x0, x1 + 1, max(1, (x1 - x0) // 4 or 1)):
            self.send(f"\x1b[<32;{x + 1};{y1 + 1}M", 0.03)
        self.send(f"\x1b[<32;{x1 + 1};{y1 + 1}M", 0.03)
        self.send(f"\x1b[<0;{x1 + 1};{y1 + 1}m", t)

    def text(self):
        return "\n".join(line.rstrip() for line in self.screen.display)

    # the main window's bottom: … rule, status, ╭─╮, │ input │, ╰─╯
    STATUS_FROM_END, INPUT_FROM_END = 4, 2

    def input_line(self):
        """the input row, inside its frame (the │ edges stripped)"""
        l = self.screen.display[-self.INPUT_FROM_END]
        return l[1:-1].rstrip() if len(l) >= 2 else l.rstrip()

    def status_line(self):
        return self.screen.display[-self.STATUS_FROM_END].rstrip()

    def status_row(self):
        return self.screen.lines - self.STATUS_FROM_END

    def find(self, s):
        """(x, y) of the first cell of `s` on screen, or None."""
        for y, line in enumerate(self.screen.display):
            x = line.find(s)
            if x >= 0:
                return x, y
        return None

    def wait_for(self, s, t=8.0):
        end = time.time() + t
        while time.time() < end:
            if s in self.text():
                return True
            self.pump(0.2)
        return False

    def stop(self):
        try:
            os.kill(self.pid, signal.SIGTERM)
            os.waitpid(self.pid, 0)
        except OSError:
            pass


def main():
    binary = os.path.abspath(sys.argv[1])
    tmp = tempfile.mkdtemp()
    log = os.path.join(tmp, "server.log")
    portfile = os.path.join(tmp, "port")
    ircd = subprocess.Popen([sys.executable, os.path.join(HERE, "fake_ircd.py"), "--log", log,
                             "--portfile", portfile, "--demo"])
    for _ in range(50):
        if os.path.exists(portfile) and open(portfile).read().strip():
            break
        time.sleep(0.1)
    port = open(portfile).read().strip()
    cfg = os.path.join(tmp, "cfg")
    os.makedirs(cfg)
    with open(os.path.join(cfg, "hexchat.conf"), "w") as f:
        f.write("net_throttle = 0\nirc_nick1 = tuismoke\ngui_slist_skip = 1\n")

    env = {"TERM": "xterm-256color", "COLORTERM": "truecolor"}
    os.environ.pop("NO_COLOR", None)
    t = Tui(([binary, "--tui"] if os.path.basename(binary) == "nexchat" else [binary]) + ["-n", "-d", cfg, f"irc://127.0.0.1:-{port}/"], env)
    fails = []

    def check(cond, what):
        if not cond:
            fails.append(what)
            print("FAIL:", what)

    def server_has(s):
        try:
            return s in open(log).read()
        except OSError:
            return False

    try:
        t.pump(2.0)
        check(t.wait_for("tuismoke"), "the nick shows (connected)")

        # keyboard: join, talk
        t.type("/join #smoke\r", 1.5)
        check(t.wait_for("#smoke"), "#smoke appears")
        t.pump(2.5)                           # the demo chatter arrives
        t.type("hello-from-tui\r", 1.0)
        check(server_has("PRIVMSG #smoke :hello-from-tui"), "the message reached the server")
        check(t.wait_for("hello-from-tui"), "our message is in the chat")

        # tab completion of a nick from the user list
        t.type("ali\t", 0.5)
        check("alice" in t.input_line(), "Tab completes a nick in the input")
        t.send(b"\x15", 0.2)                  # Ctrl-U: GUI's underline… clear the line instead
        t.send(b"\x1bu", 0.2)                 # Alt-U: kill to start

        # mouse: the user list — right-click a nick opens the nick menu
        pos = t.find("+bob") or t.find("@alice") or t.find(" alice")
        check(pos is not None, "the user list shows nicks")
        if pos:
            t.click(pos[0] + 1, pos[1], button=2)
            check(t.wait_for("Open Dialog Window", 2), "right-click a nick: the nick menu")
            p = t.find("Open Dialog Window")
            if p:
                t.click(p[0] + 2, p[1])
                t.pump(1.0)
                check(t.find("Open Dialog Window") is None, "the menu closes after a click")

        # mouse: click #smoke in the tree goes back to it
        p = t.find("#smoke")
        if p:
            t.click(p[0] + 1, p[1])
        t.pump(0.5)

        # mouse: the wheel scrolls the chat (the "↓ N more" hint appears);
        # fill it past a screen first
        for i in range(3):
            t.type("/echo filler line %d %s\r" % (i, "x" * 200), 0.2)
        t.pump(0.5)
        t.wheel(COLS // 2, ROWS // 2, up=True)
        t.wheel(COLS // 2, ROWS // 2, up=True)
        check("more" in t.text(), "the wheel scrolls back")
        # a click on the hint goes back to the bottom
        p = t.find("more")
        if p:
            t.click(p[0], p[1])
        t.pump(0.5)

        # mouse: the menu bar opens with a click, closes with Esc
        p = t.find("View")
        if p and p[1] == 0:
            t.click(p[0] + 1, 0)
            check(t.wait_for("User List", 2), "clicking View opens the View menu")
            t.send(b"\x1b", 0.6)

        # keyboard: Ctrl-G switcher
        t.send(b"\x07", 0.4)
        check(t.wait_for("Go to", 2), "Ctrl-G opens the switcher")
        t.send(b"\x1b", 0.6)
        # Ctrl-P: the command palette, every action and command
        t.send(b"\x10", 0.4)
        check(t.wait_for("Command Palette", 2), "Ctrl-P opens the command palette")
        t.type("prefer", 0.4)
        check(t.find("Preferences") is not None, "it finds menu actions (Preferences)")
        t.send(b"\r", 0.6)
        check(t.wait_for("Settings", 2), "Enter runs it")
        t.send(b"\x1b", 0.6)

        # View → Theme → Choose a scheme…: pick Dracula, saved in tui.conf
        t.send(b"\x1bv", 0.4)                       # Alt-V
        t.send(b"\x1b[A", 0.2)                      # up to Theme (last)
        t.send(b"\x1b[C", 0.3)                      # → into the submenu
        t.send(b"\x1b[B", 0.2)                      # Choose a scheme…
        t.send(b"\r", 0.4)
        t.type("Dracula", 0.4)
        check(t.wait_for("scheme", 2), "the theme picker lists maya's schemes")
        t.send(b"\r", 0.6)
        conf = os.path.join(cfg, "tui.conf")
        check(os.path.exists(conf) and "theme=Dracula" in open(conf).read(), "the theme is saved in tui.conf")

        # a dialog: /getstr prompts through fe_get_str
        t.type("/getstr HELLO \"echo got\" \"Say something\"\r", 0.8)
        check(t.wait_for("Say something", 2), "fe_get_str shows a prompt dialog")
        t.type("there\r", 0.8)

        # quit
        t.type("/quit bye\r", 1.5)
        check(server_has("QUIT"), "the QUIT reached the server")
    finally:
        screen = t.text()
        t.stop()
        ircd.terminate()

    if fails:
        print("--- screen\n" + screen)
        print("--- server.log\n" + open(log).read()[-3000:])
        sys.exit(1)
    print("tui-smoke: ok")


if __name__ == "__main__":
    main()

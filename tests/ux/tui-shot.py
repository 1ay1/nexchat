#!/usr/bin/env python3
"""Drive a TUI in a pty and print what's on screen (pyte as the terminal).

    tui-shot.py [--size COLSxROWS] [--wait S] BIN [ARGS...] -- KEYS...

KEYS are sent in order, with a short pause after each, then the screen is
printed. A key is literal text, or one of: <enter> <tab> <stab> <esc> <up>
<down> <left> <right> <pgup> <pgdn> <f2> <f3> <bs> <c-X> (Ctrl-X) <a-X>
(Alt-X) <a-up> <a-down> <sleep:S>. The screen is printed as text; with
--attrs, cells that are bold/reverse/coloured are listed too.
"""
import os, pty, select, sys, time, signal, struct, fcntl, termios

import pyte

KEYS = {
    "enter": "\r", "tab": "\t", "stab": "\x1b[Z", "esc": "\x1b", "bs": "\x7f",
    "up": "\x1b[A", "down": "\x1b[B", "right": "\x1b[C", "left": "\x1b[D",
    "pgup": "\x1b[5~", "pgdn": "\x1b[6~", "f2": "\x1bOQ", "f3": "\x1bOR",
    "a-up": "\x1b[1;3A", "a-down": "\x1b[1;3B",
}


def encode(k):
    if k.startswith("<") and k.endswith(">"):
        name = k[1:-1]
        if name.startswith("sleep:"):
            return ("sleep", float(name[6:]))
        if name in KEYS:
            return KEYS[name]
        if name.startswith("c-") and len(name) == 3:
            return chr(ord(name[2].lower()) - 96)
        if name.startswith("a-") and len(name) == 3:
            return "\x1b" + name[2]
    return k


def main():
    args = sys.argv[1:]
    cols, rows, wait, attrs = 100, 30, 0.6, False
    while args and args[0].startswith("--"):
        a = args.pop(0)
        if a == "--size":
            cols, rows = map(int, args.pop(0).split("x"))
        elif a == "--wait":
            wait = float(args.pop(0))
        elif a == "--attrs":
            attrs = True
    if "--" in args:
        i = args.index("--")
        cmd, keys = args[:i], args[i + 1:]
    else:
        cmd, keys = args, []

    screen = pyte.Screen(cols, rows)
    stream = pyte.ByteStream(screen)
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["COLORTERM"] = "truecolor"
        os.environ.pop("NO_COLOR", None)
        os.execvp(cmd[0], cmd)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    os.kill(pid, signal.SIGWINCH)

    def pump(t):
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], max(0, end - time.time()))
            if not r:
                continue
            try:
                data = os.read(fd, 65536)
            except OSError:
                return False
            if not data:
                return False
            # Answer the probes a real terminal would (cursor position,
            # device status), or the app waits for them.
            if b"\x1b[6n" in data:
                os.write(fd, b"\x1b[1;1R")
            if b"\x1b[5n" in data:
                os.write(fd, b"\x1b[0n")
            if b"\x1b[c" in data:
                os.write(fd, b"\x1b[?62;22c")
            stream.feed(data)
        return True

    pump(wait)
    for k in keys:
        e = encode(k)
        if isinstance(e, tuple):
            pump(e[1])
            continue
        os.write(fd, e.encode())
        pump(0.15)
    pump(wait)

    for line in screen.display:
        print(line.rstrip())
    if attrs:
        for y in range(rows):
            for x in range(cols):
                c = screen.buffer[y][x]
                if c.bold or c.reverse or c.fg != "default" or c.bg != "default":
                    print(f"{x},{y} {c.data!r} fg={c.fg} bg={c.bg} b={int(c.bold)} r={int(c.reverse)}")
    try:
        os.kill(pid, signal.SIGTERM)
        os.waitpid(pid, 0)
    except OSError:
        pass


if __name__ == "__main__":
    main()

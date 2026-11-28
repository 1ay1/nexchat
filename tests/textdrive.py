#!/usr/bin/env python3
"""Drive nexchat-text without fixed sleeps.

    textdrive.py OUT [--offline] [--settle S] [--timeout T] -- nexchat-text args... <<steps

Starts the client, copies everything it prints (colour codes stripped) to
OUT, and feeds it the steps on stdin, one per line:

    /command            send it, wait for its first output (max --reply s,
                        default 3), then until the output is quiet for
                        --settle seconds (default 0.4): the reply is in
    ?regex              wait until the output matches regex (max --timeout)
    !cmd                run cmd in the shell (e.g. kill a server), no wait
    =regex /command     send /command once regex has matched (e.g. logged in)

The first command waits for the client to be logged in (a 001 / "End of
MOTD" / "Connected" line or any output going quiet). After the last step,
/quit. Every wait is bounded; the whole run is bounded by --timeout * 4.
"""
import os
import re
import subprocess
import sys
import threading
import time

ANSI = re.compile(rb"\x1b\[[0-9;]*[A-Za-z]")


def main(argv):
    out_path = argv[0]
    settle, timeout, offline, reply = 0.4, 15.0, False, 3.0
    i = 1
    while argv[i] != "--":
        if argv[i] == "--offline":
            offline = True
            i += 1
            continue
        if argv[i] == "--settle":
            settle = float(argv[i + 1])
        elif argv[i] == "--reply":
            reply = float(argv[i + 1])
        elif argv[i] == "--timeout":
            timeout = float(argv[i + 1])
        i += 2
    cmd = argv[i + 1:]
    steps = [l.rstrip("\n") for l in sys.stdin if l.strip()]

    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT)
    buf = bytearray()
    last = [time.monotonic()]
    lock = threading.Lock()

    def reader():
        while True:
            b = p.stdout.read1(65536)
            if not b:
                return
            with lock:
                buf.extend(ANSI.sub(b"", b))
                last[0] = time.monotonic()

    threading.Thread(target=reader, daemon=True).start()
    deadline = time.monotonic() + timeout * 4

    def text():
        with lock:
            return buf.decode("utf-8", "replace")

    def wait_match(rx, t=timeout):
        end = min(time.monotonic() + t, deadline)
        r = re.compile(rx)
        while time.monotonic() < end and p.poll() is None:
            if r.search(text()):
                return True
            time.sleep(0.02)
        return bool(r.search(text()))

    def wait_quiet(t=timeout):
        end = min(time.monotonic() + t, deadline)
        while time.monotonic() < end and p.poll() is None:
            with lock:
                idle = time.monotonic() - last[0]
            if idle >= settle:
                return
            time.sleep(0.02)

    def wait_reply(before):
        # something new first (up to --reply s: a command can be answered
        # after a pause), then quiet: the whole answer is in
        end = min(time.monotonic() + reply, deadline)
        while time.monotonic() < end and p.poll() is None:
            with lock:
                if len(buf) > before:
                    break
            time.sleep(0.02)
        wait_quiet()

    def size():
        with lock:
            return len(buf)

    def send(line):
        try:
            p.stdin.write((line + "\n").encode())
            p.stdin.flush()
        except BrokenPipeError:
            pass

    # in: logged in (or at least the burst of connecting is over); with
    # --offline (nexchat-text -a) nothing connects by itself: just started
    if not offline:
        wait_match(r" 001 |End of /?MOTD|MOTD File is missing|Connected\.|\* Running", timeout)
    wait_quiet()
    for s in steps:
        if p.poll() is not None or time.monotonic() > deadline:
            break
        if s.startswith("?"):
            wait_match(s[1:])
        elif s.startswith("!"):
            subprocess.call(s[1:], shell=True)
        elif s.startswith("="):
            rx, _, line = s[1:].partition(" ")
            wait_match(rx)
            n = size()
            send(line)
            wait_reply(n)
        else:
            n = size()
            send(s)
            wait_reply(n)
    send("/quit")
    try:
        p.wait(timeout=3)
    except subprocess.TimeoutExpired:
        p.kill()
        p.wait()
    time.sleep(0.05)
    with open(out_path, "w") as f:
        f.write(text())


if __name__ == "__main__":
    main(sys.argv[1:])

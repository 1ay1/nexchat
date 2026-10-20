#!/usr/bin/env python3
"""The TUI's File Transfers and Raw Log, against fake_ircd and a real DCC
sender (a socket here that serves a file):

  * a DCC SEND offer arrives (fake_ircd --script), shows in File Transfers
  * Accept (mouse) downloads it; the file lands in dcc_dir, byte for byte
  * Window → Raw Log shows the lines to and from the server

    tui-transfers.py NEXCHAT_TUI
"""
import os
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from importlib import import_module
smoke = import_module("tui-smoke")

PAYLOAD = os.urandom(200_000)


def sender(sock):
    """Serve PAYLOAD once, as a DCC SEND source: write, read 4-byte acks."""
    conn, _ = sock.accept()
    conn.sendall(PAYLOAD)
    got = 0
    conn.settimeout(10)
    try:
        while got < len(PAYLOAD):
            ack = conn.recv(4)
            if not ack:
                break
            if len(ack) == 4:
                got = struct.unpack("!I", ack)[0]
    except OSError:
        pass
    conn.close()


def main():
    binary = os.path.abspath(sys.argv[1])
    tmp = tempfile.mkdtemp()
    dl = os.path.join(tmp, "downloads")
    os.makedirs(dl)

    lsock = socket.socket()
    lsock.bind(("127.0.0.1", 0))
    lsock.listen(1)
    dport = lsock.getsockname()[1]
    threading.Thread(target=sender, args=(lsock,), daemon=True).start()

    script = os.path.join(tmp, "script")
    ip = struct.unpack("!I", socket.inet_aton("127.0.0.1"))[0]
    with open(script, "w") as f:
        f.write(f":sender!s@example.org PRIVMSG {{nick}} :\x01DCC SEND payload.bin {ip} {dport} {len(PAYLOAD)}\x01\r\n")

    log, portfile = os.path.join(tmp, "server.log"), os.path.join(tmp, "port")
    ircd = subprocess.Popen([sys.executable, os.path.join(smoke.HERE, "fake_ircd.py"), "--log", log,
                             "--portfile", portfile, "--script", script])
    for _ in range(50):
        if os.path.exists(portfile) and open(portfile).read().strip():
            break
        time.sleep(0.1)
    port = open(portfile).read().strip()
    cfg = os.path.join(tmp, "cfg")
    os.makedirs(cfg)
    open(os.path.join(cfg, "hexchat.conf"), "w").write(
        "net_throttle = 0\nirc_nick1 = getter\ngui_slist_skip = 1\n"
        f"dcc_dir = {dl}\ndcc_auto_recv = 0\ngui_autoopen_recv = 1\n")
    os.environ.pop("NO_COLOR", None)
    t = smoke.Tui([binary, "-n", "-d", cfg, f"irc://127.0.0.1:-{port}/"], {"TERM": "xterm-256color"})
    fails = []

    def check(cond, what):
        if not cond:
            fails.append(what)
            print("FAIL:", what)

    def menu(item):
        p = t.find("Window")
        t.click(p[0] + 1, p[1])
        q = t.wait_for(item, 2) and t.find(item)
        if q:
            t.click(q[0] + 1, q[1])

    try:
        t.pump(2.0)
        # the Raw Log first, so it records the offer's arrival
        menu("Raw Log")
        check(t.wait_for("Raw Log", 2), "Window → Raw Log opens a Raw Log buffer")
        t.type("/join #dcc\r", 1.5)                # the script fires on this join
        p = t.find("Raw Log")                      # back to it, from the tree
        if p:
            t.click(p[0] + 1, p[1])
            t.pump(0.4)
        check(t.wait_for(">>", 2) and t.wait_for("<<", 2), "the Raw Log shows lines both ways")
        check(t.wait_for("DCC SEND payload.bin", 3), "the Raw Log has the offer")

        # dcc_auto_recv = 0: the offer is only listed; accept it there, mouse
        menu("File Transfers")
        check(t.wait_for("File Transfers", 2), "Window → File Transfers opens it")
        check(t.wait_for("payload.bin", 2), "the offer is listed")
        p = t.find("[ Accept ]")
        check(p is not None, "Accept is there for an offer")
        if p:
            t.click(p[0] + 2, p[1])
        end = time.time() + 10
        target = os.path.join(dl, "payload.bin")
        while time.time() < end and not (os.path.exists(target) and os.path.getsize(target) == len(PAYLOAD)):
            t.pump(0.3)
        check(os.path.exists(target) and open(target, "rb").read() == PAYLOAD, "the file arrived, byte for byte")
        check(t.wait_for("Done", 3), "the transfer shows Done")
        p = t.find("[ Clear ]")
        if p:
            t.click(p[0] + 2, p[1])
        t.pump(0.5)
        check(t.find("payload.bin") is None or "No transfers" in t.text(), "Clear removes finished transfers")
        t.send(b"\x1b", 0.5)
    finally:
        screen = t.text()
        t.stop()
        ircd.terminate()

    if fails:
        print("--- screen\n" + screen)
        print("--- server.log\n" + open(log).read()[-1500:])
        sys.exit(1)
    print("tui-transfers: ok")


if __name__ == "__main__":
    main()

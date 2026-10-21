#!/usr/bin/env python3
"""Link previews in the TUI (gui_url_preview): right-click an image link,
Preview Image, and the picture shows in a card, drawn in half blocks ▀
(each cell two pixels, true colour); Esc closes it. Nothing is fetched
before you ask. A link that isn't an image says why.

A local HTTP server serves a 2x2 PNG (red, green / blue, white) and a
text file named like an image (NEXCHAT_HTTPGET_ALLOW_LOCAL lets the core
fetch from loopback).

    tui-preview.py NEXCHAT_TUI
"""
import http.server
import os
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from importlib import import_module
smoke = import_module("tui-smoke")


def png(w, h, pixels):
    """pixels: rows of (r, g, b)"""
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    raw = b"".join(b"\0" + b"".join(bytes(p) for p in row) for row in pixels)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


IMG = png(2, 2, [[(255, 0, 0), (0, 255, 0)], [(0, 0, 255), (255, 255, 255)]])
hits = []


class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        hits.append(self.path)
        if self.path == "/cat.png":
            body, ctype = IMG, "image/png"
        elif self.path == "/fake.png":
            body, ctype = b"just text", "text/plain"
        else:
            self.send_response(404)
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


def main():
    binary = os.path.abspath(sys.argv[1])
    httpd = http.server.HTTPServer(("127.0.0.1", 0), H)
    hport = httpd.server_address[1]
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{hport}"

    tmp = tempfile.mkdtemp()
    log, portfile = os.path.join(tmp, "server.log"), os.path.join(tmp, "port")
    script = os.path.join(tmp, "script.irc")
    open(script, "w").write(f":alice!a@h PRIVMSG #demo :look {base}/cat.png ok\n"
                            f":bob!b@h PRIVMSG #demo :and {base}/fake.png too\n")
    ircd = subprocess.Popen([sys.executable, os.path.join(smoke.HERE, "fake_ircd.py"), "--log", log,
                             "--portfile", portfile, "--demo", "--script", script])
    for _ in range(50):
        if os.path.exists(portfile) and open(portfile).read().strip():
            break
        time.sleep(0.1)
    port = open(portfile).read().strip()
    cfg = os.path.join(tmp, "cfg")
    os.makedirs(cfg)
    open(os.path.join(cfg, "hexchat.conf"), "w").write(
        "net_throttle = 0\nirc_nick1 = tester\ngui_slist_skip = 1\ngui_url_preview = 1\n")
    os.environ.pop("NO_COLOR", None)
    smoke.COLS, smoke.ROWS = 120, 36
    t = smoke.Tui([binary, "-n", "-d", cfg, f"irc://127.0.0.1:-{port}/#demo"],
                  {"TERM": "xterm-256color", "COLORTERM": "truecolor", "NEXCHAT_HTTPGET_ALLOW_LOCAL": "1"})
    fails = []

    def check(cond, what):
        print(("ok:  " if cond else "FAIL:"), what)
        if not cond:
            fails.append(what)

    def preview(name):
        p = t.find(f"{base}/{name}")
        if not p:
            return False
        t.click(p[0] + 3, p[1], button=2)
        if not t.wait_for("Preview Image", 2):
            return False
        q = t.find("Preview Image")
        t.click(q[0] + 1, q[1])
        return True

    try:
        t.pump(2.0)
        check(t.wait_for("cat.png", 5), "the image link is in the chat")
        check(not hits, "nothing fetched before asking")
        check(preview("cat.png"), "right-click → Preview Image")
        ok = False
        for _ in range(30):
            t.pump(0.2)
            if "▀" in t.text():
                ok = True
                break
        check(ok, "the picture shows, in half blocks")
        check("/cat.png" in hits, "fetched when asked")
        # its colours: scaled up, so red at the top left and blue at the bottom left.
        # A slow runner can catch the picture half drawn: wait for the blue.
        def corners():
            cells = [(y, x, c) for y, row in t.screen.buffer.items() for x, c in row.items() if c.data == "▀"]
            if not cells:
                return None
            return (min(cells, key=lambda v: (v[0], v[1]))[2], max(cells, key=lambda v: (v[0], -v[1]))[2])
        cs = corners()
        for _ in range(25):
            if cs and cs[1].bg == "0000ff":
                break
            t.pump(0.2)
            cs = corners()
        if cs:
            c0, c1 = cs
            check(c0.fg == "ff0000", f"red at the top left (got {c0.fg})")
            check(c1.bg == "0000ff", f"blue at the bottom left (got {c1.bg})")
        t.send(b"\x1b", 0.5)
        check("▀" not in t.text(), "Esc closes it")
        check(preview("fake.png"), "a link named like an image")
        check(t.wait_for("can't show it", 4), "says it can't show what isn't an image")
        t.send(b"\x1b", 0.4)
    finally:
        screen = t.text()
        t.stop()
        ircd.terminate()
        httpd.shutdown()

    if fails:
        print("--- screen\n" + screen)
        sys.exit(1)
    print("tui-preview: ok")


if __name__ == "__main__":
    main()

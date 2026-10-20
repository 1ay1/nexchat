#!/usr/bin/env python3
"""A file host like soju's (soju.im/FILEHOST), for tests: POST /upload with
the file as the body answers 201 and a Location; the uploads and their
headers are logged. GET serves them back.

usage: fake_filehost.py --portfile F --log L [--timeout S]
"""
import argparse
import http.server
import json
import os
import threading

p = argparse.ArgumentParser()
p.add_argument("--portfile", required=True)
p.add_argument("--log", required=True)
p.add_argument("--timeout", type=int, default=60)
a = p.parse_args()
store = {}


class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(n)
        name = "f%d" % (len(store) + 1)
        store[name] = (self.headers.get("Content-Type", ""), body)
        with open(a.log, "a") as f:
            f.write(json.dumps({
                "path": self.path, "size": len(body),
                "type": self.headers.get("Content-Type"),
                "disposition": self.headers.get("Content-Disposition"),
                "auth": self.headers.get("Authorization"),
            }) + "\n")
        self.send_response(201)
        self.send_header("Location", "/files/" + name)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self):
        name = self.path.rsplit("/", 1)[-1]
        if name not in store:
            self.send_response(404)
            self.end_headers()
            return
        t, b = store[name]
        self.send_response(200)
        self.send_header("Content-Type", t)
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)


srv = http.server.HTTPServer(("127.0.0.1", 0), H)
with open(a.portfile + ".tmp", "w") as f:
    f.write(str(srv.server_address[1]))
os.rename(a.portfile + ".tmp", a.portfile)
threading.Timer(a.timeout, srv.shutdown).start()
srv.serve_forever()

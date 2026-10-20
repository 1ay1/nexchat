#!/usr/bin/env python3
"""IRCv3 STS end to end, with fe-text and a self-signed TLS server.
1. plain port says sts=port=TLS: the client must stop and come back over TLS
2. TLS port says sts=duration=...: the client keeps that (sts.conf)
3. a new plain connect to the same host goes straight to TLS
usage: tests/sts.py NEXCHAT-TEXT"""
import os, socket, ssl, subprocess, sys, tempfile, threading, time

binp = sys.argv[1]
tmp = tempfile.mkdtemp()
key, crt = os.path.join(tmp, "k.pem"), os.path.join(tmp, "c.pem")
subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", key,
                "-out", crt, "-days", "1", "-subj", "/CN=localhost"], check=True,
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def listener():
    # dual-stack where there is IPv6: "localhost" is ::1 first on Windows,
    # and a refused connect there takes ~2s before it tries 127.0.0.1
    try:
        s = socket.socket(socket.AF_INET6)
        s.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
        s.bind(("::", 0))
    except OSError:
        s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("127.0.0.1", 0))
    s.listen(4); s.settimeout(15)
    return s, s.getsockname()[1]

plain, pport = listener()
tls, tport = listener()
events = []

def irc(conn, caps, tag):
    f = conn.makefile("rb")
    try:
        while True:
            l = f.readline().decode(errors="replace").strip()
            if not l:
                return
            events.append(f"{tag}: {l}")
            if l.startswith("CAP LS"):
                conn.sendall(f":fake CAP * LS :{caps}\r\n".encode())
            elif l.startswith("CAP REQ"):
                conn.sendall(f":fake CAP * ACK :{l.split(':', 1)[1]}\r\n".encode())
            elif l.startswith("USER"):
                conn.sendall(b":fake 001 sts :hi\r\n:fake 376 sts :end\r\n")
            elif l.startswith("QUIT"):
                return
    except Exception as e:
        events.append(f"{tag}: closed {type(e).__name__}")

def serve_plain():
    while True:
        try:
            c, _ = plain.accept()
        except socket.timeout:
            return
        events.append("plain: connect")
        threading.Thread(target=irc, args=(c, f"sts=port={tport}", "plain"), daemon=True).start()

def serve_tls():
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain(crt, key)
    while True:
        try:
            c, _ = tls.accept()
        except socket.timeout:
            return
        try:
            c = ctx.wrap_socket(c, server_side=True)
        except Exception as e:
            events.append(f"tls: handshake {e}"); continue
        events.append(f"tls: connect {c.version()}")
        threading.Thread(target=irc, args=(c, "sts=duration=3600", "tls"), daemon=True).start()

for fn in (serve_plain, serve_tls):
    threading.Thread(target=fn, daemon=True).start()

cfg = os.path.join(tmp, "cfg"); os.mkdir(cfg)
open(os.path.join(cfg, "hexchat.conf"), "w").write("net_throttle = 0\nirc_nick1 = sts\n")
# servlist.conf: accept the self-signed cert for these tests
def run(url, secs):
    p = subprocess.Popen([binp, "-n", "-d", cfg, url], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    time.sleep(secs)
    try:
        p.stdin.write(b"/quit\n"); p.stdin.flush()
    except BrokenPipeError:
        pass
    time.sleep(0.5); p.kill()
    return p.stdout.read().decode(errors="replace")

fail = 0
def check(ok, what):
    global fail
    print(("ok:   " if ok else "FAIL: ") + what)
    fail |= not ok

# the -1 URL scheme: irc://host:-port means "don't ask"; TLS from STS must
# still check the cert, so trust our test CA through SSL_CERT_FILE
os.environ["SSL_CERT_FILE"] = crt
out1 = run(f"irc://localhost:-{pport}/", 4)
check(any(e == "plain: connect" for e in events), "first connect is plain")
check(any(e.startswith("tls: connect") for e in events), "STS sends it to the TLS port")
check(not any(e.startswith("plain: AUTHENTICATE") for e in events), "no SASL on the plain connection")
sts = open(os.path.join(cfg, "sts.conf")).read() if os.path.exists(os.path.join(cfg, "sts.conf")) else ""
check(f"localhost {tport} " in sts, f"policy kept in sts.conf ({sts.strip()!r})")

events.clear()
out2 = run(f"irc://localhost:-{pport}/", 3)
check(not any(e.startswith("plain:") for e in events), "next time: no plain connection at all")
check(any(e.startswith("tls: connect") for e in events), "next time: straight to TLS")

if fail:
    print("--- events"); print("\n".join(events))
    print("--- client 1"); print(out1[-1500:])
    sys.exit(1)
print("sts: ok")

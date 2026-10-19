#!/usr/bin/env python3
"""Every per-network setting (servlist.conf) survives a load and a save.

Writes a network list using every field the editor has: nicks, user,
real name, password, each login method, encoding, every flag, several
servers with a selected one, connect commands, favourite channels with
keys. Starts the text frontend, makes it save the list, and compares.
usage: tests/servlist-roundtrip.py path/to/nexchat-text
"""
import os, subprocess, sys, tempfile, time

bin_ = sys.argv[1]
d = tempfile.mkdtemp()

nets = []
# every login method (servlist.h LOGIN_*), every flag combination that
# matters (cycle 1, global 2, ssl 4, auto 8, proxy 16, invalid 32,
# favourite 64, stay online 128, bouncer 256)
for i, (login, flags) in enumerate([(0, 1|2|16), (1, 4|8), (2, 4|32), (3, 1|64),
                                    (4, 16), (5, 4|8|16), (6, 4|128), (7, 4|256),
                                    (8, 1), (9, 4), (10, 4|32|64), (11, 4),
                                    (12, 4|8), (13, 4|16|128)]):
    n = ["N=Net%02d" % i,
         "I=nick%d" % i, "i=alt%d" % i,
         "U=user%d" % i, "R=Real Name %d" % i,
         "P=pa ss:%d" % i, "L=%d" % login,
         "E=%s" % ("UTF-8 (Unicode)" if i % 2 else "ISO-8859-1 (Western Europe)"),
         "F=%d" % flags, "D=%d" % (i % 2),
         "S=irc%d.example.org/+6697" % i, "S=irc%d.example.net/6667" % i,
         "C=MSG NickServ hello %d" % i, "C=MODE nick%d +i" % i,
         "J=#chan%d,key%d" % (i, i), "J=#other%d" % i]
    nets.append(n)
text = "v=2.16.2\n\n" + "\n\n".join("\n".join(n) for n in nets) + "\n\n"
os.makedirs(d, exist_ok=True)
open(os.path.join(d, "servlist.conf"), "w").write(text)
open(os.path.join(d, "hexchat.conf"), "w").write("gui_slist_skip = 1\n")

# load it and write it straight back, as the editor's Close does
p = subprocess.Popen([bin_, "-n", "-a", "-d", d], stdin=subprocess.PIPE,
                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                     env=dict(os.environ, NEXCHAT_SERVLIST_SAVE="1"))
time.sleep(1.5)
for l in ["/quit"]:
    p.stdin.write((l + "\n").encode()); p.stdin.flush(); time.sleep(0.5)
try:
    p.communicate(timeout=5)
except subprocess.TimeoutExpired:
    p.terminate(); p.communicate()

# servlist_save also runs at exit; read back what it wrote
def blocks(t):
    out = {}
    for b in t.split("\n\n"):
        lines = [l for l in b.strip().split("\n") if l and not l.startswith("v=")]
        if lines and lines[0].startswith("N="):
            body = lines[1:]
            # no L= line is the default method (0)
            if not any(l.startswith("L=") for l in body):
                body.append("L=0")
            out[lines[0][2:]] = sorted(body)
    return out

saved = open(os.path.join(d, "servlist.conf")).read()
want, got = blocks(text), blocks(saved)
bad = 0
for name, lines in want.items():
    g = got.get(name)
    if g is None:
        print("FAIL: %s: gone" % name); bad += 1; continue
    for l in lines:
        if l not in g:
            print("FAIL: %s: %r lost (has %s)" % (name, l, [x for x in g if x[:2] == l[:2]]))
            bad += 1
    for l in g:
        if l not in lines:
            print("FAIL: %s: %r appeared" % (name, l)); bad += 1
print("servlist-roundtrip: %d networks, %d wrong" % (len(want), bad))
sys.exit(1 if bad else 0)

#!/usr/bin/env python3
"""Every setting survives a save and a restart.

Runs the text frontend in a fresh config dir, /set's every variable in
hexchat.conf to a value different from its default, quits, starts again,
and compares what /set prints with what was set.
usage: tests/prefs-roundtrip.py path/to/nexchat-text
"""
import os, re, subprocess, sys, tempfile, time

bin_ = sys.argv[1]
d = tempfile.mkdtemp()

def run(lines, secs=3):
    p = subprocess.Popen([bin_, "-n", "-d", d], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    time.sleep(1)
    for l in lines:
        p.stdin.write((l + "\n").encode()); p.stdin.flush(); time.sleep(0.01)
    time.sleep(secs)
    p.stdin.write(b"/quit\n"); p.stdin.flush()
    try:
        out, _ = p.communicate(timeout=10)
    except subprocess.TimeoutExpired:
        p.kill(); out, _ = p.communicate()
    return out.decode("utf-8", "replace")

# first start writes the defaults (any /set saves the whole file)
run(["/set -quiet irc_nick1 rtnick"], 1)
conf = os.path.join(d, "hexchat.conf")
defaults = {}
for line in open(conf, encoding="utf-8"):
    m = re.match(r"^(\S+) = ?(.*)$", line.rstrip("\n"))
    if m:
        defaults[m.group(1)] = m.group(2)

# a value for each that isn't its default
skip = {"version", "gui_lang", "irc_nick1", "irc_nick2", "irc_nick3",
        "irc_user_name", "irc_real_name",
        # on, it turns %H %S ... typed after it into colour codes
        "input_perc_color", "input_perc_ascii"}
want = {}
for k, v in defaults.items():
    if k in skip:
        continue
    if re.fullmatch(r"-?\d+", v):
        n = int(v)
        # booleans flip, numbers move a little and stay in range
        want[k] = str(1 - n) if n in (0, 1) else str(n + 1)
    else:
        want[k] = (v + "x") if v else "rt-" + k

run(["/set -quiet %s %s" % (k, v) for k, v in want.items()], 3)

# after a restart: what's in the file, and what nexchat itself reports
got = {}
for line in open(conf, encoding="utf-8"):
    m = re.match(r"^(\S+) = ?(.*)$", line.rstrip("\n"))
    if m:
        got[m.group(1)] = m.group(2)
out = run(["/set %s" % k for k in want], 3)
live = dict(re.findall(r"^(\S+)\s*\.*:\s*(.*?)\s*$", out, re.M))

bad = 0
for k, v in sorted(want.items()):
    f = got.get(k)
    l = live.get(k)
    if f != v:
        print("FAIL: %s: set %r, file has %r (default %r)" % (k, v, f, defaults[k]))
        bad += 1
    elif l is not None and l != v and not (v in ("0", "1") and l in ("OFF", "ON")
                                            and (v == "1") == (l == "ON")):
        print("FAIL: %s: set %r, after restart it's %r" % (k, v, l))
        bad += 1
print("prefs-roundtrip: %d settings, %d wrong" % (len(want), bad))
sys.exit(1 if bad else 0)

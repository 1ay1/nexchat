#!/usr/bin/env python3
"""UTF-8 and emoji in the chat box: drawn, wrapped, clicked, selected and
copied as whole characters a reader sees (grapheme clusters).

Lines with emoji (skin tones, ZWJ families, flags, keycaps), combining
accents, CJK, RTL, and broken UTF-8 (invalid bytes, truncated sequences,
overlongs) come from a fake server; then, for each line, drags from its
start to many x positions. Every copy must be valid UTF-8 and never end
inside a character, and nothing may be left half drawn.
usage: tests/ux/utf8.py path/to/nexchat
"""
import os, re, subprocess, sys, tempfile, time, unicodedata

bin_ = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))
fake = os.path.join(here, "..", "fake_ircd.py")
tmp = tempfile.mkdtemp()

GOOD = [
    "thumbs \U0001F44D\U0001F3FD skin \U0001F44B\U0001F3FF tones",
    "family \U0001F468\u200d\U0001F469\u200d\U0001F467\u200d\U0001F466 zwj",
    "flags \U0001F1E9\U0001F1EA\U0001F1EF\U0001F1F5\U0001F1FA\U0001F1F8 here",
    "keycaps 1\ufe0f\u20e3 #\ufe0f\u20e3 and \u2764\ufe0f heart",
    "accents e\u0301 a\u0300 o\u0308 n\u0303 c\u0327 combining",
    "CJK \u6f22\u5b57\u304b\u306a\u30ab\u30ca \ud55c\uad6d\uc5b4 wide",
    "RTL \u05e9\u05dc\u05d5\u05dd \u0645\u0631\u062d\u0628\u0627 mixed",
    "mixed \U0001F600text\U0001F389more\U0001F44Dend",
]
BAD = [
    b"invalid \xff\xfe bytes then ok",
    b"truncated \xe2\x82 sequence then ok",
    b"overlong \xc0\xaf slash then ok",
    b"lone continuation \x80\x80 then ok",
]

with open(os.path.join(tmp, "script"), "wb") as f:
    for i, l in enumerate(GOOD):
        f.write((":u%d!u@h PRIVMSG #u :%s\n" % (i, l)).encode())
    for i, l in enumerate(BAD):
        f.write(b":b%d!b@h PRIVMSG #u :" % i + l + b"\n")

def ends_inside(s):
    """The copy stops in the middle of a character a reader sees."""
    if not s:
        return False
    if s[-1] == "\u200d":
        return True
    ri = 0
    for ch in reversed(s):
        if 0x1F1E6 <= ord(ch) <= 0x1F1FF:
            ri += 1
        else:
            break
    return ri % 2 == 1

ROW = 16  # px per row at the default font (measured from the full shot)
fail = 0
def bad(m):
    global fail
    fail = 1
    print("FAIL: " + m)

port = os.path.join(tmp, "port")
srv = subprocess.Popen([sys.executable, fake, "--timeout", "120", "--log", os.path.join(tmp, "s.log"),
                        "--portfile", port, "--script", os.path.join(tmp, "script")],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for _ in range(50):
    if os.path.exists(port) and os.path.getsize(port):
        break
    time.sleep(0.1)

cfg = os.path.join(tmp, "cfg")
os.makedirs(cfg)
open(os.path.join(cfg, "hexchat.conf"), "w").write(
    "gui_slist_skip = 1\nirc_nick1 = u8\ngui_win_width = 900\ngui_win_height = 520\nstamp_text = 0\n"
    "gui_tab_layout = 0\n")
steps = ["wait 100"]
for row in range(2, 2 + len(GOOD) + len(BAD)):
    y = 8 + row * ROW
    # from just left of the text (after the nick column) to many points
    for x in range(40, 700, 17):
        steps += ["press 34 %d" % y, "move %d %d" % (x, y), "release %d %d" % (x, y), "check %d %d" % (x, y)]
    steps.append("dpress %d %d" % (300, y))
    steps.append("release 300 %d" % y)
    steps.append("check 300 %d" % y)
env = dict(os.environ, NEXCHAT_XDRAG=";".join(steps), NEXCHAT_XDRAG_QUIT="1",
           GTK_A11Y="none", G_DEBUG="fatal-criticals")
if os.environ.get("KEEP"):
    env["NEXCHAT_XDRAG_PNG"] = os.path.join(os.environ["KEEP"], "utf8")
try:
    r = subprocess.run([bin_, "-n", "-d", cfg, "irc://127.0.0.1:-%s/#u" % open(port).read().strip()],
                         env=env, capture_output=True, timeout=200)
    out = r.stderr
    if r.returncode < 0:
        bad("nexchat died with signal %d" % -r.returncode)
except subprocess.TimeoutExpired as e:
    out = e.stderr or b""
    bad("timed out")
srv.kill()

checks = re.findall(rb"^xdrag-step: (\d+) (\d+) (-?\d+) (.*)$", out, re.M)
if not checks:
    bad("no results (crashed?): %r" % out[-300:])
seen = set()
for x, y, diff, raw in checks:
    where = "x=%s y=%s" % (x.decode(), y.decode())
    if diff != b"0":
        bad("%s: %s pixels left half drawn" % (where, diff.decode()))
    try:
        txt = raw.decode("utf-8")
    except UnicodeDecodeError:
        bad("%s: copied text isn't valid UTF-8: %r" % (where, raw))
        continue
    if ends_inside(txt):
        bad("%s: copy ends inside a character: %r" % (where, txt[-6:]))
    seen.add(txt)
if b"CRITICAL" in out:
    bad("GTK critical: %s" % re.findall(rb".*CRITICAL.*", out)[:2])
if b"xdrag-diff" not in out:
    bad("didn't finish (crashed?)")
# the broken lines arrived readable: the bad bytes shown as � and the
# good text after them whole
for want in ("invalid \ufffd", "bytes then ok", "sequence then ok", "slash then ok", "continuation \ufffd"):
    if not any(want in s for s in seen):
        bad("broken-UTF-8 line not shown/copied right: %r" % want)
# and the good lines copy back exactly as they were sent
for want in GOOD:
    if not any(s.rstrip() == want or s.rstrip().endswith(want) for s in seen):
        bad("line doesn't copy back whole: %r" % want)

print("utf8: %d selections checked%s" % (len(checks), ", ok" if not fail else ""))
if fail and os.environ.get("KEEP"):
    open(os.path.join(os.environ["KEEP"], "utf8-steps"), "w").write(";".join(steps))
    open(os.path.join(os.environ["KEEP"], "utf8.log"), "wb").write(out)
    for s in sorted(seen)[:60]:
        print("  copied: %r" % s)
sys.exit(fail)

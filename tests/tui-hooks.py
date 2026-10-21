#!/usr/bin/env python3
"""The core hooks the TUI used to stub out, against fake_ircd --demo:

  * joins/parts in a row fold into one line (irc_group_joinpart)
  * /lastlog fills (lastlog) and goes there; -r takes a regex
  * /settext and /setcursor reach the input line (scripts use these)
  * /MENU items show in the menu bar and run their command
  * highlights and private messages become OSC 9 notifications
  * replies: Alt-R / /reply pick a message, the bar on the input, Enter
    sends it with +draft/reply

    tui-hooks.py NEXCHAT_TUI
"""
import os
import re
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from importlib import import_module
smoke = import_module("tui-smoke")


def main():
    binary = os.path.abspath(sys.argv[1])
    tmp = tempfile.mkdtemp()
    log, portfile = os.path.join(tmp, "server.log"), os.path.join(tmp, "port")
    script = os.path.join(tmp, "script.irc")
    open(script, "w").write(
        ":a1!u@h JOIN #demo\n:a2!u@h JOIN #demo\n:a3!u@h JOIN #demo\n"
        ":frank!f@example.org PART #demo :gone\n:a2!u@h QUIT :bye\n"
        ":mallory!m@example.org PRIVMSG {nick} :psst private\n"
        "@msgid=m1 :alice!a@example.org PRIVMSG #demo :the first question\n"
        "@msgid=m2 :bob!b@example.org PRIVMSG #demo :a second thought\n")
    ircd = subprocess.Popen([sys.executable, os.path.join(smoke.HERE, "fake_ircd.py"), "--log", log,
                             "--portfile", portfile, "--demo", "--script", script,
                             "--caps", "message-tags"])
    for _ in range(50):
        if os.path.exists(portfile) and open(portfile).read().strip():
            break
        time.sleep(0.1)
    port = open(portfile).read().strip()
    cfg = os.path.join(tmp, "cfg")
    os.makedirs(cfg)
    open(os.path.join(cfg, "hexchat.conf"), "w").write(
        "net_throttle = 0\nirc_nick1 = tester\ngui_slist_skip = 1\nirc_group_joinpart = 1\n"
        "input_balloon_hilight = 1\ninput_balloon_priv = 1\ngui_focus_omitalerts = 0\ngui_replies = 1\n")
    os.environ.pop("NO_COLOR", None)
    # a plain terminal: no tmux passthrough (pyte can't parse it), OSC 9
    for k in ("TMUX", "VTE_VERSION", "TERM_PROGRAM"):
        os.environ.pop(k, None)
    t = smoke.Tui([binary, "-n", "-d", cfg, f"irc://127.0.0.1:-{port}/#demo"], {"TERM": "xterm-256color"})
    raw = []
    feed = t.stream.feed

    def rec(d):
        raw.append(d)
        feed(d)
    t.stream.feed = rec
    fails = []

    def check(cond, what):
        print(("ok:  " if cond else "FAIL:"), what)
        if not cond:
            fails.append(what)

    def run(cmd):
        t.type(cmd, 0.3)
        t.send(b"\r", 0.6)

    try:
        t.pump(2.0)
        check(t.wait_for("#demo", 5), "in #demo")
        check(t.wait_for("a1, a3 joined", 5), "joins fold into one line, a2 (came and went) dropped")
        check("frank left" in t.text(), "...with the part on it")
        check(t.text().count("a1") <= 2, "a1 isn't on a line of its own")

        # notifications (OSC 9 under a plain TERM)
        out = b"".join(raw).decode("utf-8", "replace")
        check(re.search(r"\x1b\]9;Highlighted message from: bob", out) is not None, "a highlight notifies (OSC 9)")
        check(re.search(r"\x1b\]9;Private message from: mallory[^\x1b]*psst private", out) is not None,
              "a private message notifies")

        # /lastlog
        run("/lastlog green")
        check(t.wait_for("Message (lastlog)", 3), "/lastlog goes to (lastlog)")
        check("red green blue" in t.text(), "with the line that matched")
        check("morning everyone" not in t.text(), "and not the others")
        run("/lastlog -r morn[a-z]+g\\b")
        check(t.wait_for("morning everyone", 3), "-r: a regex")
        run("/lastlog -r (")
        check(t.wait_for("missing", 3) or t.wait_for("parenthes", 1), "a bad regex says why")
        t.send(b"\x07", 0.3)
        t.type("#demo", 0.3)
        t.send(b"\r", 0.5)
        check(t.wait_for("Message #demo", 3), "back in #demo")

        # the input line from scripts
        run("/settext héllo wörld")
        check("héllo wörld" in t.input_line(), "/settext fills the input line")
        t.send(b"\r", 0.4)   # sent: the line is empty again
        # typing a command replaces the line: both run later, from timers
        run("/timer 1 settext abcdef")
        run("/timer 2 setcursor 2")
        t.pump(2.5)
        t.type("X", 0.3)
        check("abXcdef" in t.input_line(), "/setcursor counts characters")
        t.send(b"\r", 0.4)

        # /MENU
        run('/menu add "Tools"')
        run('/menu add "Tools/Say it" "echo MENU-RAN"')
        check(t.wait_for(" Tools ", 2), "a /MENU top level shows in the bar")
        p = t.find(" Tools ")
        t.click(p[0] + 2, p[1])
        check(t.wait_for("Say it", 2), "with its item")
        q = t.find("Say it")
        t.click(q[0] + 1, q[1])
        check(t.wait_for("MENU-RAN", 3), "clicking it runs its command")
        run('/menu del "Tools"')
        for _ in range(10):
            if " Tools " not in t.text().splitlines()[0]:
                break
            t.pump(0.2)
        check(" Tools " not in t.text().splitlines()[0], "/menu del takes it away")

        # replies: Alt-R picks a recent message, the bar says who, Esc drops
        # it, Enter sends with +draft/reply
        t.send(b"\x1br", 0.5)
        check(t.wait_for("Reply to", 2), "Alt-R: the reply picker")
        check("bob: a second thought" in t.text(), "newest first, with who said it")
        t.type("first", 0.3)
        t.send(b"\r", 0.5)
        check("↳ alice: the first question" in t.text(), "the bar on the input says who")
        t.send(b"\x1b", 0.4)
        check("↳" not in t.text(), "Esc: not replying any more")
        run("/reply alice")
        check("↳ alice: the first question" in t.text(), "/reply alice: her last message")
        run("sure thing")
        sent = open(log).read()
        check("@+draft/reply=m1 PRIVMSG #demo :sure thing" in sent, "Enter sends it as a reply")
        check("↳" not in t.text(), "and the bar goes")
    finally:
        screen = t.text()
        t.stop()
        ircd.terminate()

    if fails:
        print("--- screen\n" + screen)
        sys.exit(1)
    print("tui-hooks: ok")


if __name__ == "__main__":
    main()

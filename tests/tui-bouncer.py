#!/usr/bin/env python3
"""The TUI's bouncer windows against real bouncers (tests/bnc/lab.sh: Ergo,
soju, ZNC on loopback), the same scenarios as the GUI's tests
(tests/ux/bncwin.sh, bncremote.sh, stayonline.sh), keys and mouse:

  1. our own soju: "soju is installed, not set up yet" → Set it up → the
     network it was opened from goes on it by itself → connects through
     it → "Nick and login..." shows who it is → switch it off → Stop/Start
  2. Use another bouncer... → I have a bouncer → address and login, checked
     as you type ("It's soju, and the login works.") → a network ticked →
     Connect: the entry is saved, it logs in, the card shows "Connected
     (soju)" and the network it's on
  3. the card: Add a network... on it; Forget asks "sure?" first

    tui-bouncer.py NEXCHAT_TUI
"""
import os
import re
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from importlib import import_module
smoke = import_module("tui-smoke")


def lab_env(tmp):
    """bnc_start in a shell; its ports back as a dict (and the pid to stop it)."""
    script = f""". "{HERE}/bnc/lab.sh"
bnc_have || exit 77
bnc_start "{tmp}/lab" >/dev/null 2>&1 || exit 1
echo "ERGO_PORT=$ERGO_PORT SOJU_PORT=$SOJU_PORT ZNC_PORT=$ZNC_PORT SOJU_BIN_DIR=$SOJU_BIN_DIR PIDS=$BNC_PIDS"
# keep them running until stdin closes
read _
bnc_stop
"""
    p = subprocess.Popen(["sh", "-c", script], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    line = p.stdout.readline()
    if not line:
        code = p.wait()
        if code == 77:
            print("tui-bouncer: bouncers not fetched (tests/bnc/fetch.sh), skipped")
            sys.exit(77)
        sys.exit("tui-bouncer: the bouncer lab didn't start")
    env = dict(kv.split("=", 1) for kv in line.split() if "=" in kv)
    return p, env


def main():
    binary = os.path.abspath(sys.argv[1])
    tmp = tempfile.mkdtemp()
    lab, env = lab_env(tmp)
    fails = []

    def check(cond, what):
        if not cond:
            fails.append(what)
            print("FAIL:", what)
        else:
            print("ok:  ", what)

    cfg = os.path.join(tmp, "cfg")
    os.makedirs(cfg)
    open(os.path.join(cfg, "hexchat.conf"), "w").write("gui_slist_skip = 1\nirc_nick1 = dee\nnet_throttle = 0\n")
    open(os.path.join(cfg, "servlist.conf"), "w").write(
        f"v=2.16.2\n\nN=Lab\nF=10\nD=0\nS=127.0.0.1/{env['ERGO_PORT']}\nJ=#den\n\n")
    os.environ.pop("NO_COLOR", None)
    run_env = {"TERM": "xterm-256color", "NEXCHAT_SOJU_DIR": env["SOJU_BIN_DIR"], "NEXCHAT_BOUNCER_NO_SYSTEMD": "1"}
    t = smoke.Tui([binary, "-n", "-d", cfg], run_env)

    def click_text(s, dx=1, button=0):
        p = t.find(s)
        if p:
            t.click(p[0] + dx, p[1], button=button)
        return p is not None

    def open_bouncer():
        p = t.find("Server")
        t.click(p[0] + 1, p[1])
        t.wait_for("Bouncer…", 2)
        click_text("Bouncer…", 2)
        return t.wait_for("╭─ Bouncer", 3)

    try:
        t.pump(2.0)
        check(t.wait_for("#den", 8), "connected to Lab directly, in #den")
        if fails:
            print(t.text())
            return

        # ── 1. our own soju ────────────────────────────────────────────
        check(open_bouncer(), "Server → Bouncer… opens the Bouncer window")
        check(t.wait_for("soju is installed, not set up yet", 3), "says soju is installed, not set up yet")
        check(click_text("[ Set it up ]", 2), "a Set it up button")
        check(t.wait_for("Your bouncer is running", 15), "one press: running")
        check(t.wait_for("in 1 channel", 30), "Lab went on it by itself (opened from Lab), soju has it in 1 channel")
        check(t.wait_for("as dee", 10), "it shows who it's on there as")
        check("[ Networks ]" in t.text() or " Networks " in t.text(), "the Networks / Channels / Access / Storage tabs")

        # Nick and login...
        check(click_text("[ Nick and login... ]", 2), "Nick and login… for Lab")
        check(t.wait_for("Your nick on Lab", 3), "the editor: Your nick on Lab")
        check("With my account (SASL)" in t.text() and "Server password" in t.text(), "login and More, as the GUI")
        click_text("[ Cancel ]", 2)
        check(t.wait_for("Your bouncer is running", 3), "Cancel: back to the window")

        # the Channels tab lists #den
        click_text(" Channels ", 2)
        check(t.wait_for("#den", 3) and "Detached" in t.text(), "Channels: #den, with Detached")
        click_text(" Networks ", 2)

        # switch Lab off: it connects directly again
        check(click_text("[ on ●]", 2), "Lab's switch is on")
        check(t.wait_for("connects directly again", 5), "switched off: connects directly again")
        check(t.wait_for("connects directly (127.0.0.1", 5), "its row: connects directly (host)")

        # Stop, then Start
        click_text("[ Stop ]", 2)
        check(t.wait_for("Your bouncer is stopped", 5), "Stop: stopped")
        click_text("[ Start ]", 2)
        check(t.wait_for("Your bouncer is running", 10), "Start: running again")
        # Remove asks first
        click_text("[ Remove... ]", 2)
        check(t.wait_for("Remove, sure?", 2), "Remove... asks: sure?")
        click_text("[ Remove, sure? ]", 2)
        check(t.wait_for("Removed. Every network connects directly again", 8), "removed")

        # ── 2. a bouncer elsewhere: the setup ──────────────────────────
        click_text("[ Use another bouncer... ]", 2)
        check(t.wait_for("I have a bouncer", 5), "Use another bouncer… → Stay online")
        click_text("[ I have a bouncer ]", 2)
        check(t.wait_for("Your bouncer", 2) and "Address" in t.text(), "the bouncer page: Address, User name, Password")
        p = t.find("Address")
        t.click(p[0] + 12, p[1])
        t.type(f"irc://127.0.0.1:{env['SOJU_PORT']}\t", 0.3)
        t.type("ayush\t", 0.3)
        t.type("secret123\t", 0.5)
        check(t.wait_for("It's soju, and the login works.", 15), "checked as you type: It's soju, and the login works.")
        click_text("[ Next ]", 2)
        check(t.wait_for("Which networks should it stay on?", 3), "Next: which networks")
        # another one: the Ergo of the lab
        p = t.find("Another one")
        t.click(p[0] + 14, p[1])
        t.type(f"irc://127.0.0.1:{env['ERGO_PORT']}\t", 0.3)
        t.type("labnet\t", 0.3)
        click_text("[ Connect ]", 2)
        conf = os.path.join(cfg, "servlist.conf")
        end = time.time() + 5
        while time.time() < end and "My bouncer (127.0.0.1)" not in open(conf).read():
            t.pump(0.3)
        check("N=My bouncer (127.0.0.1)" in open(conf).read(), "saved as My bouncer (127.0.0.1)")
        check(t.wait_for("Connected (soju)", 20), "the card: Connected (soju)")
        check(t.wait_for("labnet", 20), "the network it was told to stay on is on it")

        # ── 3. the card ────────────────────────────────────────────────
        check(click_text("[ Add a network... ]", 2), "Add a network… on the card")
        check(t.wait_for("Add a network to the bouncer", 3), "its page")
        click_text("[ OFTC ]", 2)
        check("irc.oftc.net" in t.text(), "a popular one fills in the address")
        click_text("[ Cancel ]", 2)
        p = t.find("[ Forget ]")
        if p:
            t.click(p[0] + 2, p[1])
        check(t.wait_for("Forget it, sure?", 2), "Forget asks: sure?")
        t.send(b"\x1b", 0.5)
    finally:
        screen = t.text()
        t.stop()
        try:
            lab.stdin.close()
            lab.wait(10)
        except Exception:
            lab.kill()

    if fails:
        print("--- screen\n" + screen)
        sys.exit(1)
    print("tui-bouncer: ok")


if __name__ == "__main__":
    main()

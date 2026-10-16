#!/usr/bin/env python3
"""Drive a running nexchat over D-Bus like a remote script does.

Run inside a private session bus (dbus-run-session), with nexchat started
on it and connected to fake_ircd. Checks the org.hexchat.connection and
org.hexchat.plugin methods and the hook signals, then exits 0 or 1.
usage: dbus-remote.py SERVICE SERVER_LOG
"""
import sys
import time

from gi.repository import Gio, GLib

service, server_log = sys.argv[1], sys.argv[2]
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
fails = 0


def check(cond, what):
    global fails
    print(("ok:   " if cond else "FAIL: ") + what)
    if not cond:
        fails += 1


def proxy(path, iface):
    return Gio.DBusProxy.new_sync(bus, Gio.DBusProxyFlags.NONE, None,
                                  service, path, iface, None)


def pump(secs):
    end = time.time() + secs
    ctx = GLib.MainContext.default()
    while time.time() < end:
        ctx.iteration(False)
        time.sleep(0.02)


def server_has(line, secs=5):
    end = time.time() + secs
    while time.time() < end:
        with open(server_log, errors="replace") as f:
            if any(l.rstrip("\r\n") == line for l in f):
                return True
        time.sleep(0.1)
    return False


conn = proxy("/org/hexchat/Remote", "org.hexchat.connection")
path = conn.Connect("(ssss)", "remote.py", "Remote test", "a test", "1.0")
check(path.startswith("/org/hexchat/"), "Connect returns an object path (%s)" % path)
# a second Connect from the same client gets the same object
check(conn.Connect("(ssss)", "remote.py", "Remote test", "a test", "1.0") == path,
      "Connect again: same path")

hc = proxy(path, "org.hexchat.plugin")

ctx = hc.FindContext("(ss)", "", "#remote")
check(ctx > 0, "FindContext #remote")
check(hc.SetContext("(u)", ctx), "SetContext")
check(hc.GetContext() == ctx, "GetContext after SetContext")
check(not hc.SetContext("(u)", 999999), "SetContext on a bad id is false")
check(hc.GetInfo("(s)", "channel") == "#remote", "GetInfo channel")
check(hc.GetInfo("(s)", "nick") == "remote", "GetInfo nick")

ptype, pstr, pint = hc.GetPrefs("(s)", "irc_nick1")
check(ptype == 1 and pstr == "remote", "GetPrefs irc_nick1 (string)")
ptype, pstr, pint = hc.GetPrefs("(s)", "net_throttle")
check(ptype == 3 and pint == 0, "GetPrefs net_throttle (bool)")

hc.Command("(s)", "say hello from dbus")
check(server_has("PRIVMSG #remote :hello from dbus"), "Command reaches the server")

hc.Print("(s)", "printed over dbus")
check(hc.EmitPrint("(sas)", "Channel Message", ["bob", "hi", "@"]), "EmitPrint")

check(hc.Nickcmp("(ss)", "Bob", "bob") == 0, "Nickcmp ignores case")
check(hc.Strip("(sii)", "\x0312Blue\x03 \x02Bold\x02", -1, 3) == "Blue Bold", "Strip")

fields = hc.ListFields("(s)", "channels")
# names carry their type as the first letter: schannel, pcontext, ...
check("schannel" in fields and "pcontext" in fields, "ListFields channels")
lid = hc.ListGet("(s)", "channels")
check(lid > 0, "ListGet channels")
names = []
while hc.ListNext("(u)", lid):
    names.append(hc.ListStr("(us)", lid, "channel"))
    hc.ListInt("(us)", lid, "context")
    hc.ListTime("(us)", lid, "lag")
check("#remote" in names, "list walks to #remote (%s)" % names)
hc.ListFree("(u)", lid)
check(not hc.ListNext("(u)", lid), "freed list is gone")

hc.SendModes("(asiyy)", ["bob"], 1, ord("+"), ord("v"))
check(server_has("MODE #remote +v bob"), "SendModes")

# hooks: the signal arrives on our object with the hook id
got = {}


def on_signal(p, sender, name, params):
    got.setdefault(name, []).append(params.unpack())


hc.connect("g-signal", on_signal)
cid = hc.HookCommand("(sisi)", "remotecmd", 0, "help", 3)
sid = hc.HookServer("(sii)", "PRIVMSG", 0, 0)
pid = hc.HookPrint("(sii)", "Channel Message", 0, 0)
check(cid > 0 and sid > cid and pid > sid, "Hook* return increasing ids")
hc.Command("(s)", "remotecmd one two")
hc.Command("(s)", "say trigger")   # fake_ircd echoes PRIVMSGs back as bob
pump(3)
c = got.get("CommandSignal", [])
check(c and c[0][0][:3] == ["remotecmd", "one", "two"] and c[0][2] == cid,
      "CommandSignal (%s)" % c[:1])
check(not server_has("remotecmd one two", 1), "EAT_ALL hook: command not sent")
check(got.get("ServerSignal"), "ServerSignal")
check(got.get("PrintSignal"), "PrintSignal")
hc.Unhook("(u)", cid)
hc.Command("(s)", "remotecmd three")
pump(1)
check(len(got.get("CommandSignal", [])) == 1, "Unhook stops the signal")
check(server_has("remotecmd three"), "after Unhook the command goes out")

conn2 = proxy(path, "org.hexchat.connection")
conn2.Disconnect()

# a script that exits without Disconnect: its hooks go with it
import subprocess
subprocess.run([sys.executable, "-c", """
from gi.repository import Gio
b = Gio.bus_get_sync(Gio.BusType.SESSION, None)
c = Gio.DBusProxy.new_sync(b, 0, None, %r, '/org/hexchat/Remote', 'org.hexchat.connection', None)
p = c.Connect('(ssss)', 'gone.py', 'gone', '', '')
h = Gio.DBusProxy.new_sync(b, 0, None, %r, p, 'org.hexchat.plugin', None)
h.HookCommand('(sisi)', 'gonecmd', 0, '', 3)
""" % (service, service)], check=True)
pump(1)
hc2 = proxy(conn.Connect("(ssss)", "remote2.py", "r2", "", ""), "org.hexchat.plugin")
hc2.SetContext("(u)", ctx)
hc2.Command("(s)", "gonecmd x")
check(server_has("gonecmd x"), "exited client's EAT_ALL hook is gone")
print("dbus-remote: %s" % ("ok" if not fails else "%d FAILED" % fails))
sys.exit(1 if fails else 0)

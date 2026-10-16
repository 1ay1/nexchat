#!/usr/bin/env python3
"""A fake panel for the StatusNotifierItem tray (GTK4 build).

Runs as org.kde.StatusNotifierWatcher on the session bus, waits for nexchat
to register its item, then does what a panel does: reads the icon and
tooltip, asks for the menu (AboutToShow + GetLayout) and prints it in the
tray.expected format, clicks "Blink on > Channel Message" through Event, and
prints the menu again. Exit 0 with the dump on stdout.
usage: sni-panel.py OUT    (run inside dbus-run-session, before nexchat)
"""
import sys
import time

from gi.repository import Gio, GLib

out = open(sys.argv[1], "w")
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
items = []

XML = """<node><interface name='org.kde.StatusNotifierWatcher'>
<method name='RegisterStatusNotifierItem'><arg type='s' direction='in'/></method>
<method name='RegisterStatusNotifierHost'><arg type='s' direction='in'/></method>
<property name='RegisteredStatusNotifierItems' type='as' access='read'/>
<property name='IsStatusNotifierHostRegistered' type='b' access='read'/>
<property name='ProtocolVersion' type='i' access='read'/>
</interface></node>"""


def call(conn, sender, path, iface, method, params, inv):
    if method == "RegisterStatusNotifierItem":
        name = params.unpack()[0]
        items.append(name if name.startswith(":") or "." in name else sender)
    inv.return_value(None)


def get(conn, sender, path, iface, prop):
    return {"RegisteredStatusNotifierItems": GLib.Variant("as", items),
            "IsStatusNotifierHostRegistered": GLib.Variant("b", True),
            "ProtocolVersion": GLib.Variant("i", 0)}[prop]


info = Gio.DBusNodeInfo.new_for_xml(XML)
bus.register_object("/StatusNotifierWatcher", info.interfaces[0], call, get, None)
Gio.bus_own_name_on_connection(bus, "org.kde.StatusNotifierWatcher", 0, None, None)

ctx = GLib.MainContext.default()
end = time.time() + 30
while not items and time.time() < end:
    ctx.iteration(False)
    time.sleep(0.02)
if not items:
    sys.exit("no item registered")
name = items[0]


def prop(path, iface, p):
    r = bus.call_sync(name, path, "org.freedesktop.DBus.Properties", "Get",
                      GLib.Variant("(ss)", (iface, p)), None, 0, -1, None)
    return r.unpack()[0]


SNI = "org.kde.StatusNotifierItem"
pix = prop("/StatusNotifierItem", SNI, "IconPixmap")
print("icon: %d pixmap(s), %dx%d" % (len(pix), pix[0][0], pix[0][1]) if pix else "icon: none", file=out)
menu_path = prop("/StatusNotifierItem", SNI, "Menu")
DM = "com.canonical.dbusmenu"


def dm(method, sig, args, rtype=None):
    r = bus.call_sync(name, menu_path, DM, method, GLib.Variant(sig, args),
                      GLib.VariantType(rtype) if rtype else None, 0, -1, None)
    return r.unpack() if r else None


def dump(node, depth, ids):
    nid, props, kids = node
    for k in kids:
        cid, p, sub = k
        ind = "  " * depth
        if p.get("type") == "separator":
            print(ind + "---", file=out)
            continue
        kind = {"checkmark": "check", "radio": "radio"}.get(p.get("toggle-type"), "item")
        line = '%s%s "%s"' % (ind, kind, p.get("label", ""))
        if p.get("enabled", True) is False:
            line += " insensitive"
        print(line, file=out)
        if kind in ("check", "radio"):
            print("%s  = %s" % (ind, "on" if p.get("toggle-state") == 1 else "off"), file=out)
        ids[(depth, p.get("label"))] = cid
        dump(k, depth + 1, ids)


def show():
    dm("AboutToShow", "(i)", (0,))
    rev, layout = dm("GetLayout", "(iias)", (0, -1, []), "(u(ia{sv}av))")
    ids = {}
    dump(layout, 0, ids)
    return ids


ids = show()
# blinking: /tray -i 2 (the message icon) flashes it, a NewIcon per change;
# /tray -t sets the tooltip
new_icons = []
bus.signal_subscribe(name, SNI, "NewIcon", "/StatusNotifierItem", None, 0,
                     lambda *a: new_icons.append(1))
import subprocess


def nexchat(cmd):
    subprocess.run(["gdbus", "call", "--session", "--dest", "io.github.nexchat.service",
                    "--object-path", "/org/hexchat/Remote", "--method",
                    "org.hexchat.plugin.Command", cmd], capture_output=True)


nexchat("tray -i 2")
end = time.time() + 3
while len(new_icons) < 3 and time.time() < end:
    ctx.iteration(False)
    time.sleep(0.02)
print("blink: %s" % ("yes" if len(new_icons) >= 3 else "no (%d NewIcon)" % len(new_icons)), file=out)
nexchat("tray -t hellotray")
for _ in range(20):
    ctx.iteration(False)
    time.sleep(0.02)
print("tooltip: %s" % prop("/StatusNotifierItem", SNI, "ToolTip")[2], file=out)
# click Blink on > Channel Message, like a user in the panel
dm("Event", "(isvu)", (ids[(1, "Channel Message")], "clicked", GLib.Variant("i", 0), 0))
for _ in range(20):
    ctx.iteration(False)
    time.sleep(0.05)
print("## after clicking Channel Message", file=out)
show()
out.close()

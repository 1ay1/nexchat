#!/usr/bin/env python3
"""Capture a screenshot of every main window, dialog and menu.

Runs the given client binary on a private Xvfb with the fake ircd in demo
mode, drives it with xdotool (real menus and /commands, like a user would)
and saves PNGs into --out. Used to build docs/ux-reference/ from the GTK2
build and to capture the ported builds for side-by-side comparison.

usage: tests/ux/capture.py --bin build-gtk2/src/fe-gtk/hexchat --out docs/ux-reference/gtk2
"""
import argparse
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

ap = argparse.ArgumentParser()
ap.add_argument("--bin", required=True)
ap.add_argument("--out", required=True)
ap.add_argument("--xvfb", default=shutil.which("Xvfb")
                or os.path.expanduser("~/.cache/nexchat-ref/xvfb/usr/bin/Xvfb"))
ap.add_argument("--display", default=":97")
ap.add_argument("--size", default="1100x720")
ap.add_argument("--ld-library-path", default="")
ap.add_argument("--only", default="", help="comma list of shot names")
ap.add_argument("--theme", default="", help="GTK_THEME value (gtk3/4 only)")
args = ap.parse_args()

os.makedirs(args.out, exist_ok=True)
tmp = tempfile.mkdtemp(prefix="nxux-")
env = dict(os.environ)
env["DISPLAY"] = args.display
env.pop("WAYLAND_DISPLAY", None)
env["GDK_BACKEND"] = "x11"
env["NO_AT_BRIDGE"] = "1"
env["LANG"] = "C.UTF-8"
env["LC_ALL"] = "C.UTF-8"
if args.ld_library_path:
    env["LD_LIBRARY_PATH"] = args.ld_library_path
if args.theme:
    env["GTK_THEME"] = args.theme
# keep the user's real settings/themes out of the capture
env["XDG_CONFIG_HOME"] = os.path.join(tmp, "xdg")
env["HOME"] = tmp
os.makedirs(env["XDG_CONFIG_HOME"], exist_ok=True)

procs = []


def spawn(cmd, **kw):
    p = subprocess.Popen(cmd, env=env, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, **kw)
    procs.append(p)
    return p


def cleanup():
    for p in reversed(procs):
        if p.poll() is None:
            p.send_signal(signal.SIGTERM)
            try:
                p.wait(3)
            except subprocess.TimeoutExpired:
                p.kill()
    shutil.rmtree(tmp, ignore_errors=True)


def xdo(*a, check=False):
    r = subprocess.run(["xdotool", *a], env=env, capture_output=True, text=True)
    if check and r.returncode:
        raise RuntimeError(f"xdotool {a}: {r.stderr}")
    return r.stdout.strip()


def windows():
    out = xdo("search", "--onlyvisible", "--name", ".")
    return [w for w in out.split() if w]


def name_of(w):
    return xdo("getwindowname", w)


def key(*keys, delay=0.25):
    for k in keys:
        xdo("key", "--clearmodifiers", k)
        time.sleep(delay)


def typ(text):
    xdo("type", "--delay", "8", text)
    time.sleep(0.2)


def cmd(text):
    focus_main()
    typ(text)
    key("Return", delay=0.6)


def shot(name, window="root"):
    path = os.path.join(args.out, name + ".png")
    subprocess.run(["import", "-window", window, path], env=env,
                   stderr=subprocess.DEVNULL)
    print("shot", name)


main_win = None


def find_main():
    global main_win
    for _ in range(60):
        out = xdo("search", "--onlyvisible", "--classname", "^(hexchat|nexchat)$")
        cands = [w for w in out.split() if w]
        # the main window is the biggest one with that class
        best, area = None, 0
        for w in cands:
            g = xdo("getwindowgeometry", w)
            if "Geometry:" in g:
                ww, hh = g.split("Geometry:")[1].strip().split("x")
                if int(ww) * int(hh) > area:
                    best, area = w, int(ww) * int(hh)
        if best and area > 200 * 200:
            main_win = best
            return best
        time.sleep(0.2)
    raise SystemExit("main window not found: " + repr([(w, name_of(w)) for w in windows()]))


def focus_main():
    xdo("windowactivate", "--sync", main_win)
    xdo("windowfocus", "--sync", main_win)
    time.sleep(0.15)


def new_toplevels(before):
    return [w for w in windows() if w not in before and w != main_win]


def close_new(before):
    for w in new_toplevels(before):
        xdo("windowclose", w)
    time.sleep(0.3)
    for w in new_toplevels(before):
        xdo("windowkill", w)
    time.sleep(0.2)


def dialog_shot(name, opener, settle=0.8):
    """Run opener, screenshot whatever new toplevel appears, close it."""
    before = set(windows())
    opener()
    time.sleep(settle)
    new = new_toplevels(before)
    if new:
        shot(name, new[-1])
    else:
        shot(name)
    close_new(before)
    focus_main()


def menu_shot(name, mnemonic):
    focus_main()
    key(f"alt+{mnemonic}", delay=0.6)
    shot(name)
    key("Escape", "Escape", delay=0.3)


def main():
    w, h = args.size.split("x")
    spawn([args.xvfb, args.display, "-screen", "0", f"{w}x{h}x24",
           "-nolisten", "tcp", "-dpi", "96"])
    time.sleep(1)
    # minimal WM so focus/activation and window titles behave
    ob = os.path.expanduser("~/.cache/nexchat-ref/ob/usr")
    wm = shutil.which("openbox") or (os.path.join(ob, "bin", "openbox")
                                     if os.path.exists(os.path.join(ob, "bin", "openbox")) else None)
    if wm:
        wm_env = dict(env)
        wm_env["LD_LIBRARY_PATH"] = os.path.join(ob, "lib")
        wm_env["XDG_CONFIG_DIRS"] = os.path.join(ob, "..", "etc", "xdg")
        procs.append(subprocess.Popen([wm], env=wm_env, stdout=subprocess.DEVNULL,
                                      stderr=subprocess.DEVNULL))
        time.sleep(0.8)

    portfile = os.path.join(tmp, "port")
    spawn([sys.executable, os.path.join(ROOT, "tests", "fake_ircd.py"), "--demo",
           "--timeout", "300", "--log", os.path.join(tmp, "server.log"),
           "--portfile", portfile])
    for _ in range(50):
        if os.path.exists(portfile) and open(portfile).read():
            break
        time.sleep(0.1)
    port = open(portfile).read()

    cfg = os.path.join(tmp, "cfg")
    os.makedirs(cfg)
    with open(os.path.join(cfg, "hexchat.conf"), "w") as f:
        f.write("irc_nick1 = ayush\nirc_user_name = ayush\nirc_real_name = Ayush\n"
                "net_throttle = 0\ngui_slist_skip = 1\ngui_join_dialog = 0\n"
                "gui_win_width = 1000\ngui_win_height = 640\n"
                "gui_win_left = 0\ngui_win_top = 0\ngui_win_save = 1\n"
                "gui_quit_dialog = 0\ngui_tray = 0\n"
                "stamp_text = 1\nstamp_text_format = [12:34]\n")

    only = set(filter(None, args.only.split(",")))

    def want(n):
        return not only or n in only

    # first-run network list (no slist_skip) is captured separately
    if want("network-list-firstrun"):
        cfg2 = os.path.join(tmp, "cfg2")
        os.makedirs(cfg2)
        p = spawn([args.bin, "-n", "-d", cfg2])
        time.sleep(3)
        shot("network-list-firstrun")
        p.terminate()
        p.wait()
        time.sleep(0.5)

    spawn([args.bin, "-n", "-d", cfg, f"irc://127.0.0.1:-{port}/"])
    find_main()
    time.sleep(2.5)
    focus_main()
    cmd("/join #nexchat")
    time.sleep(1.5)

    if want("main-tree"):
        shot("main-tree", main_win)

    # menus (mnemonics from mymenu[] in fe-gtk/menu.c)
    for n, m in [("menu-hexchat", "x"), ("menu-view", "v"), ("menu-server", "s"),
                 ("menu-settings", "e"), ("menu-window", "w"), ("menu-help", "h")]:
        if want(n):
            menu_shot(n, m)

    # View > Channel Switcher > Tabs
    if want("main-tabs"):
        cmd("/set -quiet gui_tab_layout 0")
        cmd("/gui apply")
        time.sleep(1)
        shot("main-tabs", main_win)
        cmd("/set -quiet gui_tab_layout 2")
        cmd("/gui apply")
        time.sleep(1)

    # dialogs opened through their commands / menus
    dialogs = [
        ("network-list", lambda: key("ctrl+s")),
        ("preferences", lambda: menu_pick("e", 1)),
        ("join-channel", lambda: menu_pick("s", 3)),
        ("channel-list", lambda: menu_pick("s", 4)),
        ("auto-replace", lambda: menu_pick("e", 2)),
        ("ctcp-replies", lambda: menu_pick("e", 3)),
        ("dialog-buttons", lambda: menu_pick("e", 4)),
        ("keyboard-shortcuts", lambda: menu_pick("e", 5)),
        ("text-events", lambda: menu_pick("e", 6)),
        ("url-handlers", lambda: menu_pick("e", 7)),
        ("user-commands", lambda: menu_pick("e", 8)),
        ("userlist-buttons", lambda: menu_pick("e", 9)),
        ("userlist-popup", lambda: menu_pick("e", 10)),
        ("ban-list", lambda: menu_pick("w", 1)),
        ("character-chart", lambda: menu_pick("w", 2)),
        ("direct-chat", lambda: menu_pick("w", 3)),
        ("file-transfers", lambda: menu_pick("w", 4)),
        ("friends-list", lambda: menu_pick("w", 5)),
        ("ignore-list", lambda: menu_pick("w", 6)),
        ("plugins", lambda: menu_pick("w", 7)),
        ("raw-log", lambda: menu_pick("w", 8)),
        ("url-grabber", lambda: menu_pick("w", 9)),
        ("search", lambda: key("ctrl+f")),
        ("save-text", lambda: menu_pick("w", 14)),
        ("load-plugin", lambda: menu_pick("x", 3)),
        ("about", lambda: menu_pick("h", 2)),
    ]
    for n, opener in dialogs:
        if want(n):
            dialog_shot(n, opener)
            if n == "search":
                # inline bar, not a window: close it so later shots are clean
                key("Escape", delay=0.4)
                focus_main()

    # preferences pages: walk the category tree with Down
    if want("preferences-pages"):
        before = set(windows())
        menu_pick("e", 1)
        time.sleep(1)
        prefs = new_toplevels(before)
        if prefs:
            pw = prefs[-1]
            # focus is on the category tree, first row selected
            for i in range(16):
                shot(f"preferences-{i:02d}", pw)
                key("Down", delay=0.4)
        close_new(before)
        focus_main()

    # popups: right-click on things inside the main window
    if want("popups"):
        popup_shots()


def menu_pick(mnemonic, index):
    """Open a top-level menu and activate its index-th selectable item."""
    focus_main()
    key(f"alt+{mnemonic}", delay=0.5)
    for _ in range(index - 1):
        key("Down", delay=0.08)
    key("Return", delay=0.4)


def popup_shots():
    geo = xdo("getwindowgeometry", main_win)
    # Position: X,Y (screen: 0)\n  Geometry: WxH
    pos = geo.split("Position:")[1].split("(")[0].strip().split(",")
    size = geo.split("Geometry:")[1].strip().split("x")
    x0, y0 = int(pos[0]), int(pos[1])
    W, H = int(size[0]), int(size[1])
    spots = {
        "popup-userlist": (x0 + W - 60, y0 + 80),
        "popup-textbox": (x0 + W // 2, y0 + H // 2),
        "popup-channel-tree": (x0 + 50, y0 + 70),
        "popup-inputbox": (x0 + W // 2, y0 + H - 15),
    }
    for n, (x, y) in spots.items():
        focus_main()
        xdo("mousemove", "--sync", str(x), str(y))
        time.sleep(0.2)
        if n == "popup-userlist":
            xdo("click", "1")
            time.sleep(0.2)
        xdo("click", "3")
        time.sleep(0.7)
        shot(n)
        key("Escape", delay=0.3)


try:
    main()
finally:
    cleanup()

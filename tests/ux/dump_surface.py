#!/usr/bin/env python3
"""Dump the non-visual UX surface to text so ports can be diffed against it.

Writes into --out:
  keybindings.txt   default key bindings (fkeys.c default_kb_cfg)
  menus.txt         main menu tree with mnemonics and accelerators (menu.c mymenu[])
  prefs.txt         every /set variable and its type (cfgfiles.c vars[])
  commands.txt      every built-in /command (outbound.c xc_cmds[])
  setup-pages.txt   preferences dialog pages and their widgets (setup.c)

It parses the C source, not a running binary, so it works for any toolkit.
"""
import argparse
import os
import re

ap = argparse.ArgumentParser()
ap.add_argument("--src", default=os.path.join(os.path.dirname(__file__), "..", "..", "src"))
ap.add_argument("--out", required=True)
a = ap.parse_args()
os.makedirs(a.out, exist_ok=True)


def read(p):
    with open(os.path.join(a.src, p), encoding="utf-8", errors="replace") as f:
        return f.read()


def c_unescape(s):
    return (s.encode("latin-1", "backslashreplace").decode("unicode_escape")
            .replace("\017", "^O").replace("\002", "^B").replace("\003", "^C")
            .replace("\035", "^I").replace("\037", "^U").replace("\026", "^R"))


# keybindings
fk = read("fe-gtk/fkeys.c")
m = re.search(r"#define default_kb_cfg \\\n(.*?)\n\n", fk, re.S)
blob = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)))
blob = c_unescape(blob)
rows = []
for rec in blob.strip().split("\n\n"):
    lines = rec.split("\n")
    accel = lines[0].split("=", 1)[1]
    action = lines[1]
    d1 = lines[2][3:] if lines[2].startswith("D1:") else ""
    d2 = lines[3][3:] if len(lines) > 3 and lines[3].startswith("D2:") else ""
    rows.append(f"{accel:32} {action:24} {d1:18} {d2}".rstrip())
open(os.path.join(a.out, "keybindings.txt"), "w").write("\n".join(rows) + "\n")

# menus
mc = read("fe-gtk/menu.c")
body = mc[mc.index("static struct mymenu mymenu[]"):]
body = body[:body.index("\n};")]
out = []
depth = 0
for line in body.splitlines():
    s = line.strip()
    if not s.startswith("{"):
        continue
    name = re.search(r'N_\s*\(\s*"((?:[^"\\]|\\.)*)"\s*(ELLIPSIS)?\s*\)', s)
    label = (name.group(1) + ("..." if name.group(2) else "")) if name else None
    kind = re.search(r"\bM_[A-Z]+", s)
    kind = kind.group(0) if kind else ""
    key = re.search(r"GDK_KEY_(\w+)\}", s)
    key = key.group(1) if key else ""
    if kind == "M_NEWMENU":
        depth = 0
        out.append(f"\n[{label}]")
        continue
    if kind == "M_END":
        depth = max(0, depth - 1)
        continue
    if kind == "M_SEP":
        out.append("  " * (depth + 1) + "----")
        continue
    if label is None:
        # entries whose label is set at runtime (detach/close)
        label = {"menu_detach": "<Detach/Attach>", "menu_close": "<Close>"}.get(
            re.search(r"\{0, (\w+)", s).group(1) if re.search(r"\{0, (\w+)", s) else "", "<runtime>")
    acc = ""
    if key:
        # same modifier rules as menu_create_main() in menu.c
        if kind == "M_MENUTOG":
            acc = {"F11": "F11"}.get(key) or ("Alt+" + key + " (Ctrl+Alt+a if _Help uses 'a')" if "MENU_ID_AWAY" in s else "Ctrl+" + key)
        elif key == "F1":
            acc = "F1"
        elif key == "w":
            acc = "Ctrl+w (Ctrl+Shift+w with Emacs key theme)"
        elif key.isupper() and len(key) == 1:
            acc = "Ctrl+Shift+" + key.lower()
        else:
            acc = "Ctrl+" + key
    tag = {"M_MENUTOG": " (toggle)", "M_MENURADIO": " (radio)"}.get(kind, "")
    out.append("  " * (depth + 1) + f"{label}{tag}" + (f"    [{acc}]" if acc else ""))
    if kind == "M_MENUSUB":
        depth += 1
open(os.path.join(a.out, "menus.txt"), "w").write("\n".join(out).strip() + "\n")

# prefs
cf = read("common/cfgfiles.c")
vs = cf[cf.index("const struct prefs vars[]"):]
vs = vs[:vs.index("\n};")]
prefs = re.findall(r'\{"(\w+)",\s*P_OFF\w+\s*\((\w+)\),\s*(TYPE_\w+)\}', vs)
open(os.path.join(a.out, "prefs.txt"), "w").write(
    "\n".join(f"{n:36} {t[5:].lower()}" for n, _, t in prefs) + "\n")

# commands
ob = read("common/outbound.c")
cs = ob[ob.index("const struct commands xc_cmds[]"):]
cs = cs[:cs.index("\n};")]
cmds = re.findall(r'\{"([A-Z0-9_]+)",\s*(\w+),\s*(\d),\s*(\d),\s*(\d)', cs)
open(os.path.join(a.out, "commands.txt"), "w").write(
    "\n".join(f"/{n:14} needserver={s} needchannel={c}" for n, _, s, c, _q in cmds) + "\n")

# preferences pages
st = read("fe-gtk/setup.c")
pages = re.findall(r"static const setting (\w+)\[\]\s*=\s*\{(.*?)\n\};", st, re.S)
lines = []
for pname, pbody in pages:
    lines.append(f"\n[{pname}]")
    for e in re.finditer(r"\{(ST_\w+),\s*N_\(\"((?:[^\"\\]|\\.)*)\"\)(?:,\s*P_OFF\w+\s*\((\w+)\))?", pbody):
        kind, label, field = e.groups()
        lines.append(f"  {kind[3:]:10} {label}" + (f"  -> {field}" if field else ""))
open(os.path.join(a.out, "setup-pages.txt"), "w").write("\n".join(lines).strip() + "\n")

print("wrote", sorted(os.listdir(a.out)))

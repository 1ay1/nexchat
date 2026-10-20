#!/usr/bin/env python3
"""Move the brand name in po/*.po from HexChat to nexchat.

The source strings changed ("HexChat" -> "nexchat"), so the old msgids no
longer match anything and those strings would show in English. For every
entry whose msgid mentions HexChat and whose rebranded msgid now exists in
the source (po/nexchat.pot when present, else the C/XML sources), rewrite
the msgid to the new text and swap the brand inside the translation, so
nothing translated is lost. Entries whose msgid is already current but whose
translation still says HexChat get the translation fixed too.

Not touched: the plugin API ("hexchat_plugin_init", the hexchat_* names,
the Perl HexChat:: package), which keep their names for compatibility.

Run from the repo root; safe to run more than once.
"""
import glob
import os
import re
import subprocess

# names that are API, not branding: never rewritten
KEEP = ["hexchat_plugin_init", "hexchat_", "HexChat::", "hexchat.conf", "hexchat-plugin"]
PROTECT = "\x00{}\x00"

STR = r'"(?:[^"\\]|\\.)*"'
ENTRY = re.compile(r'^(?P<pre>(?:#.*\n)*)msgid (?P<id>(?:' + STR + r'\n)+)msgstr (?P<str>(?:' + STR + r'\n?)+)', re.M)


def join(block):
    return "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', block))


def protect(s):
    keep = []
    for k in KEEP:
        while k in s:
            keep.append(k)
            s = s.replace(k, PROTECT.format(len(keep) - 1), 1)
    return s, keep


def unprotect(s, keep):
    for i, k in enumerate(keep):
        s = s.replace(PROTECT.format(i), k)
    return s


def rebrand(s, mnemonic=False):
    s, keep = protect(s)
    if mnemonic:
        s = re.sub(r"(?i)he(_?)x(_?)chat", lambda m: f"ne{m.group(1)}x{m.group(2)}chat", s)
    s = re.sub(r"HexCha[tr]|Hexchat|HEXCHAT|hexchat", "nexchat", s)
    return unprotect(s, keep)


def quote(text, prefix):
    """text as po lines: one line if short, else "" then pieces at \\n"""
    if len(text) < 70 and "\\n" not in text[:-2]:
        return f'{prefix} "{text}"\n'
    parts = re.split(r'(?<=\\n)', text)
    return f'{prefix} ""\n' + "".join(f'"{p}"\n' for p in parts if p)


def source_msgids():
    """every msgid the sources have now"""
    pot = "po/nexchat.pot"
    text = ""
    if os.path.exists(pot):
        text = open(pot, encoding="utf-8").read()
        return {join(m.group("id")) for m in ENTRY.finditer(text)}
    # no .pot: the strings as written in the sources is enough to tell
    out = subprocess.run(["git", "grep", "-h", "-o", "-E", r'N?_\("([^"\\]|\\.)*"', "--", "src", "plugins"],
                         capture_output=True, text=True).stdout
    ids = {m[m.index('"') + 1:-1] for m in out.splitlines()}
    for f in glob.glob("data/misc/*.in"):
        ids.update(re.findall(r">([^<]+)<", open(f, encoding="utf-8").read()))
    return ids


current = source_msgids()
changed = 0
for path in sorted(glob.glob("po/*.po")):
    src = open(path, encoding="utf-8").read()
    # msgids already in this file: a rename onto one of them would make a
    # duplicate (msgfmt refuses); the existing entry wins, the old one is
    # left for msgmerge to mark obsolete
    present = {join(m.group("id")) for m in ENTRY.finditer(src)}

    def fix(m):
        old_id = join(m.group("id"))
        msgstr = join(m.group("str"))
        if not old_id:		# the header
            return m.group(0)
        new_id = rebrand(old_id, mnemonic="_" in old_id)
        if new_id != old_id and new_id not in current and old_id in current:
            new_id = old_id	# the source still says it this way
        if new_id != old_id and new_id in present:
            new_id = old_id
        if new_id != old_id:
            present.add (new_id)
        new_str = rebrand(msgstr, mnemonic="_" in old_id)
        if new_id == old_id and new_str == msgstr:
            return m.group(0)
        return m.group("pre") + quote(new_id, "msgid") + quote(new_str, "msgstr")

    out = ENTRY.sub(fix, src)
    if out != src:
        open(path, "w", encoding="utf-8").write(out)
        changed += 1
print(f"updated {changed} po files")

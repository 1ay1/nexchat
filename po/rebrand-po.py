#!/usr/bin/env python3
"""Move the brand-name strings in po/*.po from HexChat to nexchat.

The source strings changed, so the old msgids would no longer match and those
strings would fall back to English. Rewrite the msgid to the new source text
and swap the brand name inside the existing translation, so nothing is lost.
Run from the repo root; safe to run more than once.
"""
import glob
import re

# old msgid -> new msgid (must match the C source exactly)
MSGIDS = {
    "HexChat": "nexchat",
    "He_xChat": "ne_xchat",
    "Quit HexChat?": "Quit nexchat?",
    "Open URL or execute command in an existing HexChat":
        "Open URL or execute command in an existing nexchat",
    "EXPORTCONF, exports HexChat settings": "EXPORTCONF, exports nexchat settings",
    "RECV <text>, send raw data to HexChat, as if it was received from the IRC server":
        "RECV <text>, send raw data to nexchat, as if it was received from the IRC server",
    "No hexchat_plugin_init symbol; is this really a HexChat plugin?":
        "No hexchat_plugin_init symbol; is this really a nexchat/HexChat plugin?",
}


def fix_msgstr(s, old_id):
    # the menu label keeps its Alt+X mnemonic wherever the translator put it
    if old_id == "He_xChat":
        return re.sub(r"(?i)he(_?)x(_?)chat", lambda m: f"ne{m.group(1)}x{m.group(2)}chat", s)
    return re.sub(r"HexChat|Hexchat|HEXCHAT", "nexchat", s)


changed = 0
for path in sorted(glob.glob("po/*.po")):
    src = open(path, encoding="utf-8").read()
    out = src
    for old, new in MSGIDS.items():
        # single-line msgid followed by a (possibly multi-line) msgstr
        pat = re.compile(r'^msgid "' + re.escape(old) + r'"\n(msgstr (?:"(?:[^"\\]|\\.)*"\n?)+)', re.M)

        def rep(m):
            body = m.group(1)
            return f'msgid "{new}"\n' + fix_msgstr(body, old)
        out = pat.sub(rep, out)
    if out != src:
        open(path, "w", encoding="utf-8").write(out)
        changed += 1
print(f"updated {changed} po files")

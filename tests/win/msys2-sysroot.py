#!/usr/bin/env python3
"""Download MSYS2 ucrt64 packages (with dependencies) into a sysroot, for
cross-building the Windows port on Linux with llvm-mingw.

usage: tests/win/msys2-sysroot.py DEST pkg [pkg ...]
       e.g. DEST=~/.cache/nexchat-win  gtk4 openssl libxml2
Packages are named without the mingw-w64-ucrt-x86_64- prefix. The files
end up under DEST/ucrt64 (bin, include, lib, share).
"""
import io
import os
import re
import subprocess
import sys
import tarfile
import urllib.request

REPO = "https://repo.msys2.org/mingw/ucrt64/"
PFX = "mingw-w64-ucrt-x86_64-"


def zstd_decompress(data):
    return subprocess.run(["zstd", "-dc"], input=data, capture_output=True, check=True).stdout


def load_db(dest):
    path = os.path.join(dest, "ucrt64.db")
    if not os.path.exists(path):
        urllib.request.urlretrieve(REPO + "ucrt64.db", path)
    raw = zstd_decompress(open(path, "rb").read())
    db = {}
    provides = {}
    with tarfile.open(fileobj=io.BytesIO(raw)) as t:
        for m in t.getmembers():
            if not m.name.endswith("/desc"):
                continue
            text = t.extractfile(m).read().decode()
            fields = {}
            key = None
            for line in text.splitlines():
                if line.startswith("%") and line.endswith("%"):
                    key = line[1:-1]
                    fields[key] = []
                elif line and key:
                    fields[key].append(line)
            name = fields["NAME"][0]
            db[name] = fields
            for p in fields.get("PROVIDES", []):
                provides[re.split("[<>=]", p)[0]] = name
    return db, provides


def resolve(db, provides, wanted):
    out, todo = [], [PFX + w if not w.startswith(PFX) else w for w in wanted]
    seen = set()
    while todo:
        n = re.split("[<>=]", todo.pop())[0]
        n = n if n in db else provides.get(n, n)
        if n in seen:
            continue
        seen.add(n)
        if n not in db:
            print(f"warning: no package {n}", file=sys.stderr)
            continue
        out.append(n)
        todo.extend(db[n].get("DEPENDS", []))
    return out


def main():
    dest = os.path.expanduser(sys.argv[1])
    os.makedirs(os.path.join(dest, "pkgs"), exist_ok=True)
    db, provides = load_db(dest)
    for n in resolve(db, provides, sys.argv[2:]):
        fn = db[n]["FILENAME"][0]
        local = os.path.join(dest, "pkgs", fn)
        if not os.path.exists(local):
            print("get", fn)
            urllib.request.urlretrieve(REPO + fn, local)
        stamp = local + ".done"
        if os.path.exists(stamp):
            continue
        raw = zstd_decompress(open(local, "rb").read()) if fn.endswith(".zst") else open(local, "rb").read()
        with tarfile.open(fileobj=io.BytesIO(raw)) as t:
            members = [m for m in t.getmembers() if m.name.startswith("ucrt64/")]
            t.extractall(dest, members=members, filter="tar")
        open(stamp, "w").close()


main()

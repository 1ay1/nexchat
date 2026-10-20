#!/usr/bin/env python3
"""Pack PNGs into a Windows .ico (PNG-compressed entries, Vista and later).

usage: make-ico.py out.ico in16.png in32.png in48.png in256.png ...
Each PNG is stored as is; its size comes from its IHDR.
"""
import struct
import sys

out, pngs = sys.argv[1], sys.argv[2:]
data = [open(p, "rb").read() for p in pngs]
entries, offset = [], 6 + 16 * len(data)
for d in data:
    assert d[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    w, h = struct.unpack(">II", d[16:24])
    # 256 is written as 0 in the directory
    entries.append(struct.pack("<BBBBHHII", w % 256, h % 256, 0, 0, 1, 32, len(d), offset))
    offset += len(d)
with open(out, "wb") as f:
    f.write(struct.pack("<HHH", 0, 1, len(data)))
    f.write(b"".join(entries))
    f.write(b"".join(data))

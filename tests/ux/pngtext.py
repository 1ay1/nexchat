#!/usr/bin/env python3
"""Print a PNG as coarse text art plus its most common colours, so a
screenshot can be checked from a terminal with no image viewer.
usage: tests/ux/pngtext.py FILE.png [STEP]"""
import struct
import sys
import zlib
from collections import Counter


def load(path):
    d = open(path, "rb").read()
    i, idat = 8, b""
    while i < len(d):
        n, = struct.unpack(">I", d[i:i + 4])
        t, c = d[i + 4:i + 8], d[i + 8:i + 8 + n]
        if t == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", c[:10])
        if t == b"IDAT":
            idat += c
        i += 12 + n
    bpp = {6: 4, 2: 3}[ctype]
    raw, stride, rows, prev, o = zlib.decompress(idat), w * bpp, [], bytearray(w * bpp), 0
    for _ in range(h):
        f = raw[o]; o += 1
        line = bytearray(raw[o:o + stride]); o += stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, bpp, rows


def main():
    w, h, bpp, rows = load(sys.argv[1])
    step = int(sys.argv[2]) if len(sys.argv) > 2 else 6
    colours = Counter(tuple(r[x:x + 3]) for r in rows for x in range(0, w * bpp, bpp))
    print(f"{w}x{h}", " ".join("#%02x%02x%02x:%d" % (*c, n) for c, n in colours.most_common(5)))
    for y in range(0, h, step * 2):
        print("".join(" .:-=+*#%@"[min(9, int((255 - sum(rows[y][x * bpp:x * bpp + 3]) / 3) / 25.6))]
                      for x in range(0, w, step)))


if __name__ == "__main__":
    import signal
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    main()

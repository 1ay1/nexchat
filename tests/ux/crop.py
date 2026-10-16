#!/usr/bin/env python3
"""Crop and scale a PNG (stdlib only) for pixel comparisons.

usage: crop.py in.png out.png X Y W H [SCALE]
Only reads the 8-bit RGB, non-interlaced PNGs that xwd2png.py writes.
"""
import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat = 8, b""
    w = h = 0
    while pos < len(data):
        (n,) = struct.unpack(">I", data[pos:pos + 4])
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        if tag == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and ctype == 2, "only 8-bit RGB"
        elif tag == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * 3
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - 3] if x >= 3 else 0
            b = prev[x]
            c = prev[x - 3] if x >= 3 else 0
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
        rows.append(bytes(line))
        prev = line
    return w, h, rows


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(tag, body):
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xffffffff)

    open(path, "wb").write(b"\x89PNG\r\n\x1a\n"
                           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


if __name__ == "__main__":
    src, dst, x, y, cw, ch = sys.argv[1], sys.argv[2], *map(int, sys.argv[3:7])
    s = int(sys.argv[7]) if len(sys.argv) > 7 else 1
    w, h, rows = read_png(src)
    out = []
    for yy in range(y, min(y + ch, h)):
        r = rows[yy][x * 3:min(x + cw, w) * 3]
        r = b"".join(r[i:i + 3] * s for i in range(0, len(r), 3))
        out.extend([r] * s)
    write_png(dst, len(out[0]) // 3, len(out), out)

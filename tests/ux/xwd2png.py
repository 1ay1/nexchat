#!/usr/bin/env python3
"""Convert a 24/32-bit ZPixmap .xwd (what `xwd -root` gives on Xvfb) to PNG.

stdlib only, so the UX scripts work on a box without ImageMagick/PIL.
usage: xwd2png.py in.xwd out.png
"""
import struct
import sys
import zlib


def read_xwd(path):
    data = open(path, "rb").read()
    hdr = struct.unpack(">25I", data[:100])
    (header_size, _ver, fmt, depth, width, height, _xoff, byte_order,
     _unit, _bit_order, _pad, bpp, bytes_per_line, _vclass, red_mask,
     green_mask, blue_mask, _bits_rgb, _cmap_entries, ncolors) = hdr[:20]
    if fmt != 2 or bpp not in (24, 32):
        raise SystemExit(f"unsupported xwd: format={fmt} bpp={bpp}")
    off = header_size + ncolors * 12
    px = data[off:]
    step = bpp // 8
    endian = "<" if byte_order == 0 else ">"

    def shift(mask):
        s = 0
        while mask and not mask & 1:
            mask >>= 1
            s += 1
        return s

    rs, gs, bs = shift(red_mask), shift(green_mask), shift(blue_mask)
    rows = []
    for y in range(height):
        line = px[y * bytes_per_line:(y + 1) * bytes_per_line]
        out = bytearray(b"\x00")  # PNG filter type 0
        for x in range(width):
            chunk = line[x * step:x * step + step]
            if step == 4:
                (v,) = struct.unpack(endian + "I", chunk)
            else:
                v = int.from_bytes(chunk, "little" if byte_order == 0 else "big")
            out += bytes(((v & red_mask) >> rs, (v & green_mask) >> gs,
                          (v & blue_mask) >> bs))
        rows.append(bytes(out))
    return width, height, b"".join(rows)


def write_png(path, w, h, raw):
    def chunk(tag, body):
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xffffffff)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


if __name__ == "__main__":
    w, h, raw = read_xwd(sys.argv[1])
    write_png(sys.argv[2], w, h, raw)

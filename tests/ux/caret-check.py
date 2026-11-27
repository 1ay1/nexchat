#!/usr/bin/env python3
"""tests/ux/caret.sh's checks: the cursor box the app reports (xcaret:
lines) against the pixels it drew. usage: caret-check.py DIR NAME ..."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngtext import load as png_load  # stdlib only, like the other tests


class Pixels:
    """p[x, y] -> (r, g, b), the way the checks below index a picture"""
    def __init__(self, path):
        self.w, self.h, self.bpp, self.rows = png_load(path)

    def __getitem__(self, xy):
        x, y = xy
        o = x * self.bpp
        return tuple(self.rows[y][o:o + 3])


def pixels(path):
    return Pixels(path)

bad = 0


def chk(c, m):
    global bad
    print(("ok:   " if c else "FAIL: ") + m)
    bad |= not c


def lin(v):
    v /= 255
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def lum(c):
    return 0.2126 * lin(c[0]) + 0.7152 * lin(c[1]) + 0.0722 * lin(c[2])


def cr(a, b):
    x, y = sorted((lum(a), lum(b)), reverse=True)
    return (x + 0.05) / (y + 0.05)


def check(d, name):
    # the input box's own background: the far end of the input row
    def bg_of(p, x, y, h):
        return p[680, y + h // 2]
    rect = {}
    for line in open(d + "/log", errors="replace"):
        if line.startswith("xcaret: "):
            f = line.split()[1:]
            rect[int(f[0])] = None if f[1] == "none" else tuple(map(int, f[1:]))
    shots = {1: "end", 2: "mid", 3: "emoji", 4: "start"}
    for k, s in shots.items():
        r = rect.get(k)
        chk(r is not None and r[0] == 2, "%s: a block cursor at %s %s" % (name, s, r))
        if not r or r[0] != 2:
            continue
        _, x, y, w, h = r
        p = pixels("%s/%s.png" % (d, s))
        inside = [p[i, j] for i in range(x + 1, x + w - 1) for j in range(y + 1, y + h - 1)]
        bg = bg_of(p, x, y, h)
        # the box's colour: what its top and bottom rows (above and below
        # the letters) are
        edge = [p[i, y] for i in range(x, x + w)] + [p[i, y + h - 1] for i in range(x, x + w)]
        fill = max(set(edge), key=edge.count)
        # most of it differs from the background (the rest: the letter in
        # the background colour; an emoji draws its own colours)
        frac = sum(1 for c in inside if cr(c, bg) > 1.5) / len(inside)
        need = {"mid": 0.4, "start": 0.4, "end": 0.9, "emoji": 0.3}[s]
        chk(frac > need and cr(fill, bg) > 3,
            "%s: %s box filled (%.0f%%, %.1f:1 on the background)" % (name, s, frac * 100, cr(fill, bg)))
        # just outside the box: not more box. Looked at on its top and
        # bottom rows, where the next letter's ink (the same colour as the
        # box) doesn't reach; one of the two clear on each side
        def clear(cx):
            return any(p[cx, cy] != fill for cy in (y, y + h - 1))
        chk(clear(x - 2) and clear(x + w + 1),
            "%s: %s box ends at its edges" % (name, s))
        if s in ("mid", "start"):
            through = [c for c in inside if cr(c, fill) > 3]
            chk(len(through) > 6, "%s: the letter under it shows through (%d px)" % (name, len(through)))
    if all(rect.get(k) for k in (1, 2, 3, 4)):
        wl, we, wm = rect[4][3], rect[1][3], rect[3][3]
        chk(4 <= wl <= 12, "%s: over 'h' a letter wide (%d px)" % (name, wl))
        chk(4 <= we <= 12, "%s: past the end a digit wide (%d px)" % (name, we))
        chk(wm >= 1.6 * wl, "%s: the whole emoji with its skin tone covered (%d px vs %d)" % (name, wm, wl))
        chk(rect[4][1] < rect[2][1] < rect[3][1] < rect[1][1], "%s: it moves with the cursor" % name)
        # unfocused: the same place, an outline (all four edges) and not filled
        _, x, y, w, h = rect[4]
        p = pixels("%s/inactive.png" % d)
        bgc = bg_of(p, x, y, h)
        ink = lambda c: cr(c, bgc) > 2
        edges = [p[x + w // 2, y], p[x + w // 2, y + h - 1], p[x, y + h // 2], p[x + w - 1, y + h // 2]]
        corners_in = [p[x + 2, y + 2], p[x + w - 3, y + h - 3], p[x + w - 3, y + 2]]
        chk(all(ink (c) for c in edges) and sum(1 for c in corners_in if not ink (c)) >= 2,
            "%s: hollow box when the window isn't focused" % name)


args = sys.argv[1:]
for i in range(0, len(args), 2):
    check(args[i], args[i + 1])
sys.exit(bad)

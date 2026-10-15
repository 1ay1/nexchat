#!/usr/bin/env python3
"""Put reference and port screenshots side by side for review.

usage: tests/ux/compare.py docs/ux-reference/gtk2 /tmp/port-shots /tmp/compare
Writes one <name>.png per shot present in both dirs (left = reference,
right = port) plus index.txt listing missing shots on either side.
"""
import os
import subprocess
import sys

ref, port, out = sys.argv[1:4]
os.makedirs(out, exist_ok=True)
r = {f for f in os.listdir(ref) if f.endswith(".png")}
p = {f for f in os.listdir(port) if f.endswith(".png")}
lines = []
for f in sorted(r & p):
    subprocess.run(["magick", os.path.join(ref, f), os.path.join(port, f),
                    "-background", "#ff00ff", "-splice", "4x0", "+append",
                    os.path.join(out, f)], check=True)
    lines.append(f"both    {f}")
for f in sorted(r - p):
    lines.append(f"MISSING {f} (not captured from port)")
for f in sorted(p - r):
    lines.append(f"EXTRA   {f} (no reference)")
open(os.path.join(out, "index.txt"), "w").write("\n".join(lines) + "\n")
print("\n".join(l for l in lines if not l.startswith("both")) or "all shots paired")

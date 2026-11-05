#!/usr/bin/env python3
"""usage: crop.py OUT.png x0 y0 x1 y1 scale A.png [B.png ...]
Writes the crops side by side (A | B | ... | mask of A!=B over tol 8), upscaled."""
import importlib.util
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[5]
spec = importlib.util.spec_from_file_location("rc", REPO / "scripts" / "render-compare.py")
rc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rc)

out = sys.argv[1]
x0, y0, x1, y1, s = map(int, sys.argv[2:7])
imgs = [rc.read_png(p) for p in sys.argv[7:]]
cw, ch = x1 - x0, y1 - y0


def px(img, x, y):
    w, h, bpp, data = img
    i = (y * w + x) * bpp
    return data[i:i + 3]


panels = []
for img in imgs:
    panels.append([[px(img, x0 + x, y0 + y) for x in range(cw)] for y in range(ch)])
if len(imgs) >= 2:
    mask = []
    for y in range(ch):
        row = []
        for x in range(cw):
            a, b = panels[0][y][x], panels[1][y][x]
            d = max(abs(a[c] - b[c]) for c in range(3))
            row.append(bytes((255, 0, 255)) if d > 8 else bytes((a[0] // 3, a[1] // 3, a[2] // 3)))
        mask.append(row)
    panels.append(mask)
gap = 4
W = len(panels) * cw * s + (len(panels) - 1) * gap
H = ch * s
buf = bytearray(W * H * 3)
for pi, p in enumerate(panels):
    ox = pi * (cw * s + gap)
    for y in range(H):
        for x in range(cw * s):
            v = p[y // s][x // s]
            i = (y * W + ox + x) * 3
            buf[i:i + 3] = v
rc.write_png(out, W, H, bytes(buf), 3)
print(out, W, H)

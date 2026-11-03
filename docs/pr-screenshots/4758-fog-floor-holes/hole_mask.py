"""Hole pixels of the treated high-ground capture against its controls.

A hole pixel is black (every channel <= 3) in the treated capture and not black
in the untreated soft-LOS capture of the same pose. Reports how each control
paints those pixels, the hole components, and writes an overlay.
"""
import sys
from collections import deque
from PIL import Image

treated, plain, hard, flat, overlay = sys.argv[1:6]
T = Image.open(treated).convert("RGB")
P = Image.open(plain).convert("RGB")
H = Image.open(hard).convert("RGB")
F = Image.open(flat).convert("RGB")
w, h = T.size
t, p, hd, f = T.load(), P.load(), H.load(), F.load()
black = lambda c: max(c) <= 3
holes = set()
treatedBlackNotPlain = 0
plainBlackNotTreated = 0
for y in range(h):
    for x in range(w):
        tb, pb = black(t[x, y]), black(p[x, y])
        if tb and not pb:
            holes.add((x, y))
        if pb and not tb:
            plainBlackNotTreated += 1
print(f"capture={w}x{h} holePixels={len(holes)} plainBlackButTreatedLit={plainBlackNotTreated}")
print(f"holes black in hard-LOS control={sum(black(hd[x, y]) for x, y in holes)}")
print(f"holes black in fully revealed (flat) control={sum(black(f[x, y]) for x, y in holes)}")
seen, comps = set(), []
for start in holes:
    if start in seen:
        continue
    q, comp = deque([start]), []
    seen.add(start)
    while q:
        x, y = q.popleft()
        comp.append((x, y))
        for n in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if n in holes and n not in seen:
                seen.add(n)
                q.append(n)
    comps.append(comp)
sizes = sorted(len(c) for c in comps)
print(f"components={len(comps)} minPx={sizes[0]} medianPx={sizes[len(sizes)//2]} maxPx={sizes[-1]}")
out = T.copy()
o = out.load()
for x, y in holes:
    o[x, y] = (255, 40, 40)
out.save(overlay)

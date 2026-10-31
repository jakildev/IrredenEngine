#!/usr/bin/env python3
"""Executable rejection witness for depth-slab extrema as a lost-record fallback.

Slabs no wider than the receiver's throw window answer depth-only interval
existence exactly: a window spans a slab boundary, so its lower partial slab
needs that slab's maximum, its upper partial slab the minimum, and interior
slabs occupancy. The finite-face query is not depth-only. Faces are recorded
per texel at texel centres, and a receiver inside the texel asks whether a
face footprint covers *its* sun-space position. Keeping one minimum and one
maximum per slab discards the middle face, and the two extrema can both be
rejected for reasons unrelated to depth: footprint and throw.

Exact rational arithmetic; no shader rounding is modelled. Exit 0 prints the
witness and the storage arithmetic; exit 1 means the witness no longer holds.
"""

from fractions import Fraction as F
import sys

THROW = F(64)          # kSunShadowMaxDistance
BIAS = F(4, 1024)      # kShadowBiasQuantNoise
TEXEL = (F(0), F(1))   # one sun texel spanning u in [0, 1); centre 0.5
SLAB = THROW           # slabs no wider than the window


def face(u_lo, u_hi, depth):
    return {"u": (u_lo, u_hi), "depth": depth}


def covers(f, u):
    return f["u"][0] <= u <= f["u"][1]


def blocks(f, u, receiver_depth):
    separation = receiver_depth - f["depth"]
    return covers(f, u) and BIAS < separation < THROW


def exact(faces, u, receiver_depth):
    return any(blocks(f, u, receiver_depth) for f in faces)


def slab_extrema(faces, texel):
    """Per slab, keep the min- and max-depth faces among those covering the texel centre."""
    centre = (texel[0] + texel[1]) / 2
    slabs = {}
    for f in faces:
        if not covers(f, centre):
            continue
        slab = f["depth"] // SLAB
        lo, hi = slabs.get(slab, (f, f))
        slabs[slab] = (f if f["depth"] < lo["depth"] else lo, f if f["depth"] > hi["depth"] else hi)
    return slabs


def extrema_answer(slabs, u, receiver_depth):
    """A footprint-honest reader of the retained extrema: it still checks each
    retained face's own footprint and throw at the receiver."""
    return any(blocks(f, u, receiver_depth) for pair in slabs.values() for f in pair)


def main():
    receiver_depth = F(10)
    # Three faces share the receiver's slab: the slab's extrema cover only the
    # texel's left half, the discarded middle face covers the right half.
    near = face(F(0), F(1, 2) + F(1, 100), F(9))      # slab maximum, left half only
    middle = face(F(1, 2) - F(1, 100), F(1), F(5))    # discarded, covers the right half
    low = face(F(0), F(1, 2) + F(1, 100), F(1))       # slab minimum, left half only
    far = face(F(0), F(1), F(-60))                    # another slab, beyond the throw window
    faces = [near, middle, low, far]
    u = F(9, 10)                                      # receiver in the right half of the texel
    exact_blocked = exact(faces, u, receiver_depth)
    slabs = slab_extrema(faces, TEXEL)
    retained = sorted({id(f) for pair in slabs.values() for f in pair})
    fallback_blocked = extrema_answer(slabs, u, receiver_depth)
    print(f"receiver u={u} depth={receiver_depth}; faces (u range, depth):")
    for name, f in (("near", near), ("middle", middle), ("low", low), ("far", far)):
        print(f"  {name:6s} u=[{f['u'][0]},{f['u'][1]}] depth={f['depth']} "
              f"blocks={blocks(f, u, receiver_depth)}")
    print(f"slabs retained {len(retained)} of {len(faces)} faces; "
          f"exact={exact_blocked} extrema={fallback_blocked}")
    # Depth-only existence over the same slabs is correct: some depth in the window exists.
    depths = sorted(f["depth"] for f in faces if covers(f, (TEXEL[0] + TEXEL[1]) / 2))
    depth_only = any(BIAS < receiver_depth - d < THROW for d in depths)
    print(f"depth-only interval existence over all faces: {depth_only}")
    texels = 2 * 1024 * 1024
    slabs_needed = int((F(1024) / SLAB))             # encoded depth range / slab width
    bytes_extrema = texels * slabs_needed * 2 * 4
    print(f"storage: {slabs_needed} slabs x 2 words per texel = {bytes_extrema:,} bytes "
          f"against 48 bytes per retained record")
    ok = exact_blocked and not fallback_blocked and depth_only
    print("WITNESS HOLDS" if ok else "WITNESS BROKEN")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

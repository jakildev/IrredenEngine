# Canvas-owned SDF descriptor storage

Captured 2026-09-22 on Apple M4 Max / Metal at 2560×1440.
Before: renderer at parent `c6b5ff7e724b99bf60d2a4945171cb3e338a8b77`.
After: implementation in this directory’s commit. The baseline build retained
only the type-only scheduler access helper; the two renderer headers were
restored from the parent. That helper does not change the parent registration.

All four before/after pairs have **zero differing RGB pixels**, comparing full
frames without masks or tolerance. These are preservation controls, not proof
that the existing SDF surface shading or shadow edges are correct. Sphere face
speckles remain. No performance improvement is claimed.

## Recipes

Build with `fleet-build --target IRCanvasStress -j3`, then run:

```sh
fleet-run --timeout 60 IRCanvasStress --only shadowbox,floor --probe-analytic-sphere --probe-analytic-canvases 3 --pivot-origin --no-spin --no-auto-rotate --no-ao --subdivisions 3 --zoom 2.5 --auto-profile --auto-screenshot 6 --sweep-yaw 0 0.78539816 2
fleet-run --timeout 60 IRCanvasStress --only shadowbox,floor --probe-analytic-box --probe-sdf-depth-tie --pivot-origin --no-spin --no-auto-rotate --no-shadows --no-ao --subdivisions 1 --zoom 4 --auto-profile --auto-screenshot 120 --sweep-yaw 0 0.78539816 2
```

Both runs reported CLEAN for both builds. Mixed capture pairs: 2231/2223 and
2232/2224 (before/after); tie pairs: 2229/2227 and 2230/2228.
An older tie control (2214) differs from both current parent and implementation;
it is not used as this change’s baseline.

| Control | Before | After |
|---|---|---|
| Three analytic canvases, yaw 0 | ![](mixed-0-before.png) | ![](mixed-0-after.png) |
| Three analytic canvases, yaw 45 | ![](mixed-45-before.png) | ![](mixed-45-after.png) |
| Coincident boxes, shadows off, yaw 0 | ![](tie-0-before.png) | ![](tie-0-after.png) |
| Coincident boxes, shadows off, yaw 45 | ![](tie-45-before.png) | ![](tie-45-after.png) |

## Scope

Descriptors and their projection snapshot survive uploads for other canvases.
The same canvas-owned buffer feeds the existing producer passes. Winning texel
references, overwrite validity, linear lighting payload and finite fragment
receiver queries remain follow-up work; no fragment reads this storage yet.

GPU storage is 80 bytes times each canvas’s power-of-two descriptor capacity,
with the existing 8192-descriptor cap. Capacity is reused, empty submissions
invalidate the snapshot, and canvas destruction frees it. Multiple active
canvases may collectively exceed the former shared 655360-byte allocation.

# Analytical box lighting normals

Native Apple M4 Max / Metal, IRCanvasStress, full unmodified PNG captures.
Before: parent d3675f7a5bd9c44e67dbb3e157872b70433b9b7a (2245/2246 are the verified identical parent captures).
After: this branch, 2257 onward.

Common arguments: `--only shadowbox,floor --probe-analytic-box --pivot-origin --no-spin --no-auto-rotate --no-ao --subdivisions 3 --zoom 2.5 --auto-screenshot 6`.

| Captures | Additional arguments | Observation |
|---|---|---|
| 2245/2246 before, 2257/2258 after | `--sweep-yaw 0 0.78539816 2` | Lighting now uses the finite box normal instead of its display slot. Shadow outlines remain jagged. |
| 2259–2262 | `--debug-overlay normals --sweep-yaw 0 4.71238898 4` | Four cardinal world-normal views. |
| 2263–2266 | same plus `--no-shadows` | Each RGB image is pixel-identical to its shadows-on counterpart. |
| 2267–2270 | `--no-shadows --debug-overlay normals --sweep-yaw 0.78539816 5.49778714 4` | Intermediate quadrants retain signed world-axis face normals. |

Normal colors encode `normal * 0.5 + 0.5`; engine upward faces have negative world Z.
These captures certify compute-sample normal routing, not fragment-level ownership or sharp shadow edges. No smoothing filter was added. OpenGL native smoke remains pending.

Mixed `--only maingrid,floor` controls at 0/45 degrees (2271/2272) are RGB pixel-identical to parent 2255/2256. Both exited CLEAN; occupied per-axis dispatch was exercised at 45 degrees.

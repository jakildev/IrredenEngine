# Shared surface lighting contract

Native Apple M4 Max / Metal. Full unmodified screenshots, all runs exited CLEAN.
This is an output-preserving consolidation, not a new shadow-edge fix.

Common IRCanvasStress arguments: `--pivot-origin --no-spin --no-auto-rotate --no-ao --subdivisions 3 --zoom 2.5 --auto-screenshot 6 --sweep-yaw 0 0.78539816 2`.

| Capture | Additional arguments | Result |
|---|---|---|
| 2273/2274 | `--only shadowbox,floor --probe-analytic-box` | Pixel-identical RGB to parent 2257/2258 in the box-normal evidence directory. |
| 2275/2276 | `--only shadowbox,floor --probe-source-box --probe-overlapping-source` | Native compute plus continuous source-face fragment lighting smoke; no before/after equivalence claim for this scene. |
| 2277/2278 | `--only maingrid,floor` | Pixel-identical RGB to parent controls 2271/2272; occupied per-axis route at 45 degrees. |

Parent code: 15ba460505c313066e63386f2de94af391582272.
The source-caster shadow has a geometric stepped silhouette; floor-shadow outlines remain visibly jagged and unaccepted for final sharp-edge goals. Native OpenGL remains pending.

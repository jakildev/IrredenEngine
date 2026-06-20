# Plan: render: SDF analytical-solver temporal jitter — curved shapes shimmer under camera Z-yaw

- **Issue:** #1920
- **Date:** 2026-06-19

**Model:** opus
**Part of:** epic #1881 (rotated-voxel correctness under camera Z-yaw)
**Blocked by:** (none) — independent work; shares `c_shapes_to_trixel` / `ir_iso_common` with sibling shader children, so serialize per #1881's one-at-a-time rule.
**Related:** #1884 (depth unification), #1883 (per-axis scatter) — distinct render paths; validated by the jitter harness child.

## Why (motivation)
On IRShapeDebug, curved SDF shapes (sphere, ellipsoid, cone, torus, cylinder, curved-panel) visibly **vibrate/shimmer** as the camera Z-yaws, while flat-faced **boxes stay stable**. Confirmed in the committed macOS reference `creations/demos/shape_debug/test/references/macos-debug/zoom4_yaw45_inter_cardinal.png`: concentric depth-band rings appear on the ellipsoid and sphere at inter-cardinal yaw and largely vanish at the start cardinal (`zoom4_origin.png`) — the artifact is **angle- and shape-type-dependent**.

Root cause (source-verified): `engine/render/src/shaders/c_shapes_to_trixel.glsl` solves per-tile depth analytically and quantizes via `stableCeilToInt(x) = int(ceil(x - kCeilBiasEpsilon))` (`:109-111`) at ~15 sites across the box/sphere/cylinder/ellipsoid/cone/torus solvers (`:267,282,285,302,317,320,344,361,369,398,419,427,468,502,510`). The fixed `kCeilBiasEpsilon = 1.0e-3` bias cannot absorb FP/FMA-reorder noise near knife-edge (near-tangent) entry distances. Curved surfaces have many near-tangent entries per silhouette, so a depth band flips between adjacent integers frame-to-frame as the continuous yaw sweeps — the rings crawl = visible vibration. Flat box faces have few near-tangent entries, so they are stable.

Distinct from the per-axis scatter seams (#1883) and the merged detached-canvas shimmer fix (#1909) — this is the SDF **analytical** depth-solve path, currently uncovered.

## Scope
Stabilize the analytical depth quantization in `c_shapes_to_trixel.glsl` so curved SDF shapes are temporally stable under a continuous yaw sweep — no crawling depth bands — while keeping the cardinal fast path byte-identical.

## Approach (investigate; the planner/worker picks)
- Characterize the instability: inspect the entry-distance values feeding `stableCeilToInt` for a sphere across a fine yaw sweep; locate where `ceil` flips per frame.
- Candidate fixes (evaluate, do not assume):
  - Slope-aware bias: replace the fixed `kCeilBiasEpsilon` with a bias ∝ |d(entry)/d(yaw)| or the surface grazing angle, so near-tangent entries get a proportionally larger snap deadband.
  - Quantize depth from a yaw-stable intermediate (solve in a rotation-invariant frame, then project) rather than from the post-rotation float entry.
  - Per-cell depth hysteresis/deadband across frames (only if it doesn't reintroduce visible lag).
- Reuse the canonical `roundHalfUp` / iso helpers; do **not** add a second ad-hoc rounding convention (see the consolidation child).

## Acceptance criteria
- Under the jitter harness child, every SDF shape (box, sphere, cylinder, ellipsoid, cone, torus, wedge, curved-panel) is temporally stable across a ≤1°-step yaw sweep at zooms {4,8,16}: frame-to-frame interior/silhouette delta within the smooth-motion threshold; no crawling depth bands.
- Cardinal (0/90/180/270) output byte-identical to current (fast path preserved).
- Both backends (Metal + OpenGL).
- No coverage/solidity regression vs the epic's `perf-grid-rotate-sweep` baseline.

## Files (start)
`engine/render/src/shaders/c_shapes_to_trixel.glsl` (stableCeilToInt + solvers); Metal mirror `engine/render/src/shaders/metal/c_shapes_to_trixel.metal`; `engine/prefabs/irreden/render/systems/system_shapes_to_trixel.hpp` (yaw snapshot); `creations/demos/shape_debug/main.cpp` (repro shots).

## Gotchas
- Keep the cardinal fast path byte-identical (epic hard constraint).
- CPU↔GPU: any rounding change must mirror between GLSL and Metal and match `IRMath::roundHalfUp`.
- Don't fix the bands by blurring — they must be **stable**, not hidden.

## References
Epic #1881; #1883 (per-axis scatter, distinct path); #1909 (detached shimmer, merged, distinct path); `stableCeilToInt` at `c_shapes_to_trixel.glsl:109-111`.

# Plan: render — per-axis scatter defect (coverage bands + face-alignment seams)

- **Issue:** #1883 (**BROADENED** — was "per-axis face seams/slivers" only)
- **Model:** opus
- **Date:** 2026-06-17
- **Epic:** #1881 — see .fleet/plans/issue-1881.md
- **Blocked by:** #1882 (the harness must isolate render paths + surface fine artifacts first)

## Scope — the per-axis forward-scatter defect at GENUINELY-ROTATING poses
This ticket is the per-axis forward-scatter path at `|residual| > deadband`
(the textures are allocated and the scatter actually draws). It is **distinct**
from the cardinal coverage HOLES at 90°/180° — those were a residual-yaw gate
mismatch in the SINGLE-CANVAS path, fixed under #1882 (the deadbanded
`computeYawSplit`); do not re-attribute them here. Validate against the #1882
harness, reading the near-cardinal *residual* tier (path=peraxis), not the
settled cardinals (path=single, now clean). Two symptoms, one root cause:

**A. Coverage "bands"** (IRPerfGrid `--mode dense` 64^3): see-through density loss
across the residual band — whole diagonal bands of the solid read as background. The
scatter fails to **fill** the rotated faces.

**B. Face-alignment seams** (canvas_stress small cubes, zoomed; human screenshots):
- a **vertical seam splitting a face into two slightly-offset panels**;
- a **doubled sliver/ridge along the top↔side face edge** (the faces don't meet — a
  few-px gap/overlap);
- **stray thin diagonal lines** on faces;
- **staircase/jagged edges** where a straight cube edge should be.
Worst **approaching** a cardinal (small residual). The scatter quads fail to **tile**.

## Root cause (shared)
The per-axis scatter deform/dilation + placement math:
- #1878's adaptive dilation margin is **anisotropic** (a single scalar across both
  in-plane axes) + **discontinuous** (hard `degenSin` gate) → over-grows the
  non-degenerate axis (stray lines) and under-fills near degeneracy (bands).
  `v_peraxis_scatter.glsl:199-222` + `metal/peraxis_scatter.metal`.
- **Float-ulp split**: the scatter consumes `isoPixelToPos3D` UN-rounded
  (`v_peraxis_scatter.glsl:165`) while resolve/lighting consume it ROUNDED →
  region-boundary shift (horizontal offsets / seams). `c_resolve_per_axis_screen_depth`.
- Cross-canvas shared edges have **no deterministic geometric owner** (reconciled
  only by planar depth + the 0.25 margin bias) → the top↔side sliver/ridge.

## Approach
- Make the dilation margin **per-axis + continuous** (grow each in-plane axis by its
  own collapse, drop the hard `degenSin` gate) — fixes both stray lines (B) and
  under-fill bands (A).
- **Unify the rounding**: consume `isoPixelToPos3D` the same way (rounded) in scatter
  + resolve/lighting — fixes the region-offset seams.
- Give the cross-canvas shared edge a **deterministic owner** (stable axis/slot tie)
  instead of an ulp-sensitive depth race — fixes the top↔side sliver/ridge.

## Affected files
- `engine/render/src/shaders/v_peraxis_scatter.glsl:165,199-222` + `metal/peraxis_scatter.metal`
- `engine/render/src/shaders/ir_iso_common.glsl` `scatterConservativeDilation` ~L752-777 (+ metal)
- `engine/render/src/shaders/c_resolve_per_axis_screen_depth.glsl` (+ metal) — rounding parity

## Acceptance criteria
- **A:** IRPerfGrid dense — coverage stays solid across the residual band (no bands).
- **B:** canvas_stress small cube — clean faces through the near-cardinal band: no
  vertical seams, no top↔side sliver, no stray lines; bounded silhouette raggedness
  (ROI before/after).
- Both backends.

## Gotchas
- The margin-depth bias must still keep grown margin yielding to a real exact
  footprint (don't bloat the silhouette).
- Don't regress #1878's band-clipping fix at large residual.

## Verification
`bash scripts/dev/perf-grid-rotate-sweep` (post-#1882 harness: residual-band coverage
for A + the zoomed near-cardinal tier for B), both hosts; canvas_stress ROI crops
before/after.

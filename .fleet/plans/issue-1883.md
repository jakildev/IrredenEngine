# Plan: render — per-axis face seams / slivers / stray lines

- **Issue:** #1883
- **Model:** opus
- **Date:** 2026-06-16
- **Epic:** #1881 — see .fleet/plans/issue-1881.md
- **Blocked by:** #1882 (shared ir_iso_common + framebuffer surface — serialized)
- **Gated on:** PR #1880 (harness) on master

## Scope
Fix the per-axis forward-scatter seams/slivers/stray-lines/staircase on zoomed
small rotated cubes (canvas_stress 48-55), worst near a face-flip.

## Affected files
- engine/render/src/shaders/v_peraxis_scatter.glsl:165,199-222 + metal/peraxis_scatter.metal
- engine/render/src/shaders/ir_iso_common.glsl scatterConservativeDilation ~L752-777 (+ metal)
- engine/render/src/shaders/c_resolve_per_axis_screen_depth.glsl (+ metal) — rounding parity

## Approach
Make the dilation margin per-axis (grow each in-plane axis by its own collapse,
not the max) and continuous (drop the hard degenSin gate). Unify the rounding:
consume isoPixelToPos3D the same way (rounded) in scatter + resolve/lighting.
Give the cross-canvas shared edge a deterministic owner (stable axis/slot tie)
instead of an ulp-sensitive depth race.

## Acceptance criteria
- Add a zoomed shot tier (zoom 3-4) to the harness; per-axis-pose zoomed perim
  drops near the cardinal-0 level; ROI crops show no slivers/stray-lines/offsets.
- canvas_stress small cube reads clean through the residual band (ROI before/after).
- Both backends.

## Gotchas
Margin-depth bias must still yield grown margin to a real exact footprint (don't
bloat silhouette); don't regress #1878's band-clipping fix at large residual.

## Verification
bash scripts/dev/perf-grid-rotate-sweep with the new zoom tier on both hosts;
canvas_stress ROI crops; cross-host IRShapeDebug smoke.

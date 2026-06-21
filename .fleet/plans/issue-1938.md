# Plan: Metal parity port of analytic scatter coverage (C2)

- **Issue:** #1938
- **Model:** opus
- **Date:** 2026-06-21
- **Epic:** #1933 — see `~/.fleet/plans/issue-1933.md` for full context
- **Blocked by:** #1937

## Scope

Faithfully port #1937's edge-type-aware analytic scatter coverage from the GL
shaders into the Metal backend, keeping the two hand-mirrored scatter shaders at
feature parity. No new design.

## Verified current state

- The Metal scatter shaders mirror the GL ones:
  `engine/render/src/shaders/metal/peraxis_scatter.metal:118-258` (vertex,
  `v_peraxis_scatter`) + `:269-296` (fragment, `f_peraxis_scatter`), calling
  `scatterConservativeDilation` at `:198-200`.
- The shared `kScatter*` constants + dilation helper live in
  `engine/render/src/shaders/metal/ir_iso_common.metal:636-717` (mirror of the
  GLSL helper).
- Metal pipeline: single-sample, no conservative-raster API (metal-cpp exposes
  only sample positions + rate maps — neither is conservative raster). Pipeline
  built in `engine/render/src/metal/metal_pipeline.cpp`.

## Affected files

- `engine/render/src/shaders/metal/peraxis_scatter.metal` — mirror #1937's
  vertex (visit-bound dilation, true-edge + interior/boundary varyings,
  occupied-neighbor tap) and fragment (analytic coverage + hard discard) changes.
- `engine/render/src/shaders/metal/ir_iso_common.metal` — mirror the analytic
  coverage helper added to `ir_iso_common.glsl`.

## Approach

1. Diff #1937's final GL shader changes; translate each to Metal syntax
   (`float2`/`float3`, `fwidth`, varying/stage-in struct fields, `discard_fragment()`).
2. Keep the `kScatter*` constants in sync (do not retire — that is #1939).
3. Run the `backend-parity` skill audit to confirm the analytic logic matches
   the GL side and no drift slipped in.

## Acceptance criteria

- Metal renders coarse-cube near-cardinal poses with the same crisp-corner /
  no-dashing result #1937 proved on GL.
- `backend-parity` audit passes; cardinal fast path byte-identical on Metal.
- Metal validation layer clean apart from the pre-existing stencil-attachment
  assert on master.

## Gotchas

- Metal has no conservative-rasterization API — stay in the analytic /
  fragment-discard model (the reason the epic chose it); no sample positions /
  rate maps.
- Don't half-retire constants — keep Metal `kScatter*` in lockstep with GL until
  #1939 removes both together.
- Metal-validation-layer stencil-attachment assert is pre-existing on master
  (fleet memory) — not a regression from this port.

## Verification

- macOS host: build the engine with the affected demo, run with
  `--auto-screenshot`; capture the same near-cardinal coarse-cube shots as #1937
  and compare GL↔Metal.
- `backend-parity` skill (the primary gate for this child).

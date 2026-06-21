# Plan: analytic edge-aware coverage on the per-axis scatter — GL backend (C1)

- **Issue:** #1937
- **Model:** opus
- **Date:** 2026-06-21
- **Epic:** #1933 — see `~/.fleet/plans/issue-1933.md` for full context (approach decision + capability audit + cross-system audit)
- **Blocked by:** (none)

## Scope

The de-risking + first-implementation phase of epic #1933. Move the per-axis
scatter coverage *decision* off the uniform conservative-dilation margin and onto
per-fragment **edge-type-aware analytic coverage**, on the OpenGL backend only.
Keep the quad dilation purely as a rasterization visit-bound.

## Verified current state

- The scatter is a rasterized instanced draw (`drawElementsInstanced(TRIANGLES,…)`,
  `engine/prefabs/irreden/render/systems/system_trixel_to_framebuffer.hpp:279`),
  one instance per per-axis cell, 3 passes (X/Y/Z).
- `v_peraxis_scatter.glsl:133-280` recovers the face origin, projects its 4 corners
  via `pos3DtoPos2DIsoYawed`, dilates them via `scatterConservativeDilation`
  (`:217`, floor `kScatterDilateMarginPx=0.85`, per-axis `0.5·|n|`, mitered).
- `f_peraxis_scatter.glsl:1-72` classifies margin-vs-interior via `vQuadParam`
  [0,1]² and applies the margin depth-yield bias.
- Corner spikes: on foreshortened near-cardinal faces `0.5·|n|` reaches a
  cell-deep fraction → corner over-extension with no single working margin (the
  #1883 `M·C ≲ 2px` vs `C ≈ half-cell` mutual-exclusion). Repro:
  `scripts/dev/perf-grid-rotate-sweep` on coarse GRID cubes.

## Affected files

- `engine/render/src/shaders/v_peraxis_scatter.glsl` — reduce dilation to a
  minimal fixed visit-bound; emit true (undilated) edge-equation varyings + a
  per-edge interior/boundary flag from an occupied-same-plane-neighbor tap.
- `engine/render/src/shaders/f_peraxis_scatter.glsl` — replace margin-vs-interior
  classification with edge-type-aware analytic coverage + hard-threshold discard.
- `engine/render/src/shaders/ir_iso_common.glsl` — add the analytic-coverage
  helper (signed-distance / coverage math) next to the existing dilation helper.
  **Do not yet retire** the margin/miter/yield constants (that is C3) — but the
  analytic coverage must already be the authority removing spikes/dashing.

## Approach

1. **Visit-bound dilation:** shrink `scatterConservativeDilation`'s use at
   `v_peraxis_scatter.glsl:217` to a minimal fixed outward grow (enough that the
   rasterizer visits every pixel the true footprint could touch — ~1px is the
   target), removing the coverage-deciding role from the geometry.
2. **True-edge varyings:** pass the 4 true (undilated) corner positions (or the
   4 edge half-plane equations) in framebuffer/clip space as flat/smooth
   varyings.
3. **Edge classification:** per the cell's grid position (`gl_InstanceID`) and an
   occupied-neighbor tap of the per-axis canvas data, mark each of the 4 edges
   interior (occupied same-plane neighbor) or boundary.
4. **Fragment coverage:** per edge, signed distance d; interior edge →
   conservative inclusion (`d > -0.5px`); boundary edge → analytic sub-pixel
   coverage `clamp(0.5 - d/fwidth(d), 0, 1)`. Combine (min across edges);
   **hard-threshold** the result for the R32I-distance / depth co-sort write
   (`discard` below ~0.5), since the depth metric is a single per-pixel value.
5. Finalize the exact thresholds/formula against `perf-grid-rotate-sweep`
   (the safety net) — corner spikes gone, no dashing, cardinal byte-identical.

## Acceptance criteria

- Coarse-cube (3³/12³) near-cardinal: corner spikes gone AND foreshortened faces
  solid (no dashing) — `perf-grid-rotate-sweep` + `shape_debug`.
- Cardinal fast path byte-identical to master (analytic term collapses to the
  exact footprint at zero residual).
- No coverage regression on the epic #1881 rotated-solidity baseline.
- GL only; Metal is #1938.

## Gotchas

- R32I/depth co-sort → hard threshold + discard, NOT alpha blend; keep depth
  co-sorted with the SDF #1370 metric.
- Bind-point budget full (0–30) — no new buffer/texture; varyings + existing
  per-axis canvas reads only.
- Shares `ir_iso_common.glsl` with #1920 — serialize per #1881.

## Verification

- `fleet-build --target IRShapeDebug` (and the perf_grid demo target).
- `bash scripts/dev/perf-grid-rotate-sweep <build_dir>` — solidity/silhouette.
- `attach-screenshots` + `render-debug-loop` on the affected demo (render-path
  change) for before/after near-cardinal coarse-cube captures.
- `render-verify` for the cardinal byte-identity gate.

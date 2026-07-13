# Plan: conservative rasterization / analytic coverage on the per-axis scatter pass — root fix for corner spikes + silhouette dashing

- **Issue:** #1933 (epic umbrella)
- **Model:** opus
- **Date:** 2026-06-21

## Scope

The per-axis voxel scatter pass loses sub-pixel coverage on foreshortened /
rotated faces. The current mitigation is a **manual conservative-dilation
margin** in the scatter vertex shader that grows each cell's rhombus outward,
plus a small tower of fragment-side heuristics to make that over-fill behave.
This margin is fighting a sub-pixel-rasterization problem with a geometry hack,
and it has a proven dead end: the near-cardinal **corner spikes** on coarse
cubes have **no local fix** (#1883 / #1917) because crisp corners need
`M·C ≲ 2px` while foreshortened coverage forces `C ≈ half-cell` — mutually
exclusive under a *uniform* margin.

This epic replaces the uniform-margin **coverage decision** with **portable
per-fragment analytic coverage** so a face's coverage is correct at the source,
eliminating corner spikes AND silhouette dashing together and removing the
hot-path geometry-hack tower.

## Verified current state (source-confirmed)

The per-axis scatter is a **rasterized instanced draw**, not a compute scatter:
`drawElementsInstanced(TRIANGLES, …)` in
`engine/prefabs/irreden/render/systems/system_trixel_to_framebuffer.hpp:279`,
one instance per per-axis cell, three passes (X/Y/Z). Each instance emits a
deformed face quad; the vertex stage dilates the quad corners; the fragment
stage classifies margin-vs-interior and applies a depth bias.

The coverage heuristics, all in `engine/render/src/shaders/ir_iso_common.glsl`
(GLSL) mirrored in `engine/render/src/shaders/metal/ir_iso_common.metal`:

| Symbol | Value | Line (GLSL) | Origin | Role |
|---|---|---|---|---|
| `scatterConservativeDilation()` | — | :768 | #1494/#1538/#1883 | grows each quad corner outward by a per-axis miter |
| `kScatterDilateMarginPx` | 0.85 px | :691 | #1494 | fixed camera-path margin floor |
| `kScatterDetachedPitchFraction` | 0.5·pitch | :738 | #1538 | pitch-proportional floor, **detached scatter only** |
| `kScatterMiterLimit` | 2.0 | :723 | #1538 | caps acute-corner blow-out |
| `kScatterMarginDepthBiasKey` | 0.25 key | :701 | #1457 | margin fragments yield to exact footprints (sub-pixel tie-break) |
| `kScatterMarginYieldGradScale` | 3.0 | :717 | #1883 | margin yields harder the further it extrapolated the face plane |

Per-axis margin = `max(floor, 0.5·|n|)` per edge (`:785`), mitered (not additive)
so acute sliver tips don't cancel (`:791-808`). On foreshortened near-cardinal
faces `0.5·|n|` reaches a cell-deep fraction — **that** is the corner spike, and
why no single margin value works (the #1883 mutual-exclusion).

**Confirmed repro path:** `scripts/dev/perf-grid-rotate-sweep` (the #1882
rotated-solidity harness) on coarse GRID cubes shows the near-cardinal corner
spikes the #1917 PR accepts as documented drift pending this root fix.

## Approach decision — analytic coverage (the other two candidates ruled out)

The issue title offers three candidate models (HW conservative rasterization /
MSAA / analytic coverage). **Source-verified capability audit picks analytic
coverage as the only portable, shared-contract option:**

1. **Hardware conservative rasterization — OFF the table.**
   - **Metal has no conservative-rasterization API at all** (metal-cpp exposes
     only programmable sample positions + `MTLRasterizationRateMap` — neither is
     conservative raster). macOS simply can't do it.
   - **OpenGL's is a vendor extension** (`GL_NV_conservative_raster` /
     `GL_INTEL_conservative_rasterization`), never core. The engine targets
     **GL 4.5 core** with **WSLg Mesa-d3d12 as the floor** across the fleet's
     primary build host (`engine/window/include/irreden/window/ir_glfw_window.hpp`),
     where these extensions are not guaranteed. Zero references exist today.
   - A GL-conservative-raster / Metal-something split has **no shared CPU
     contract** — exactly what the issue asks to avoid.

2. **MSAA — OFF the table.**
   - The pass writes a **color (RGBA8) + an integer distance texture (R32I) +
     depth**; the R32I distance and depth are the co-sort metric consumed
     downstream. MSAA-resolving an *integer* distance / depth is not an average —
     it needs per-sample storage + a custom min/max resolve compute pass.
   - 4×/8× multisampled canvas textures = 4–8× memory on every canvas, on a hot
     path, plus a per-frame resolve. Far heavier than the defect warrants.
   - Nothing in either backend is multisampled today (all single-sample).

3. **Analytic per-fragment coverage — PICKED.** Fully portable (no extension, no
   new GPU resource, no multisample targets), identical on GL + Metal (the
   shaders are mirrors), and it directly breaks the #1883 mutual-exclusion:
   **decouple the rasterization footprint (the quad dilation, kept only as a
   "visit enough pixels" bound) from the coverage decision (exact, per-fragment,
   analytic).** With coverage computed analytically against the *true* footprint,
   the dilation can be generous without spiking corners, because the analytic
   term trims each fragment back to the true face.

### The coverage model

Make coverage **edge-type-aware** — this is what removes the mutual-exclusion
that a *uniform* margin could not:

- **Interior edges** (shared with an occupied same-plane neighbor cell): fill
  **conservatively** (round outward ~half a pixel) so the inter-cell waffle /
  cracks the #1494 margin was added to close stay closed.
- **Boundary / silhouette edges** (the face's outer edge — no occupied
  neighbor): use **exact sub-pixel analytic coverage** (a smooth
  `coverage = clamp(0.5 - signedDist/fwidth, 0, 1)`-style edge term, hard-
  thresholded for the R32I/depth co-sort write). No outward over-extension →
  **corners no longer spike**; sub-pixel partial coverage along the silhouette →
  **no dashing** (the binary pixel-center test that drops sub-pixel slivers is
  gone).

A corner is where two **boundary** edges meet, so it gets exact treatment on
both edges → no spike. A foreshortened silhouette is a boundary edge → analytic
sub-pixel coverage → solid. Interior shared edges → conservative fill → no
cracks. The vertex stage already knows each cell's grid position (`gl_InstanceID`)
and reads the per-axis canvas data, so the **occupied-neighbor tap** that
classifies each edge interior-vs-boundary is an incremental read, and the true
(undilated) edge equations are passed as varyings.

With analytic coverage authoritative, the fragment-side **margin-vs-interior
machinery becomes redundant** — every surviving fragment is interior, so the
margin depth-bias (`kScatterMarginDepthBiasKey`) and yield-gradient
(`kScatterMarginYieldGradScale`), and the dilation margin/miter/pitch-fraction
constants, are retired or reduced to the minimal visit-bound (child 3).

## Decomposition (serial chain — shared shaders, #1881 one-at-a-time rule)

All three children touch `ir_iso_common.{glsl,metal}` +
`{v_,f_}peraxis_scatter.glsl` / `peraxis_scatter.metal`, so per epic #1881's
"one shader change at a time" rule they **must serialize** (each `Blocked by:`
its predecessor). They also serialize against the sibling SDF-jitter work
(#1920) which shares `ir_iso_common` — see Gotchas.

| Phase | Child | Model | Effort | Blocked by |
|---|---|---|---|---|
| C1 | render: analytic edge-aware coverage on the per-axis scatter (GL) | opus | high | (none) |
| C2 | render: Metal parity port of analytic scatter coverage | opus | medium | #1937 |
| C3 | render: retire the manual scatter dilation margin/miter/yield tower + full validation | opus | high | #1938, #1922 |

- **C1 (GL, de-risk + impl):** add the edge-type-aware analytic coverage to
  `{v_,f_}peraxis_scatter.glsl` + the helper in `ir_iso_common.glsl`; keep a
  minimal fixed dilation as the visit-bound. Prove corner spikes gone + no
  dashing on coarse cubes (3³/12³) near-cardinal via `perf-grid-rotate-sweep`
  + `shape_debug`; cardinal fast-path byte-identical; no coverage regression.
  GL first (the leading backend; new render work lands GLSL first).
- **C2 (Metal parity):** mirror C1 into `peraxis_scatter.metal` +
  `ir_iso_common.metal`; run the `backend-parity` audit. Pure port — no new
  design.
- **C3 (retire + validate):** with analytic coverage authoritative on both
  backends, retire/simplify the redundant heuristics (the six symbols in the
  table above, across camera + detached regimes, plus the fragment
  margin-vs-interior classification). Validate temporal stability under the
  **#1922 jitter harness** + epic #1881 rotated-solidity gate on both backends;
  update the `engine/render/CLAUDE.md` convex-corner drift note (the
  #1917-accepted drift is now fixed). Gated on #1922 being on master because
  "temporally stable" is unverifiable without that harness.

## Cross-system audit (consumers of the symbols C3 retires)

All consumers are **shader-side only** — the margin constants are compile-time
`const`/`constant` in the shaders; there is **no CPU-side uniform plumbing** to
migrate (`grep` over `engine/**/*.{hpp,cpp}` finds none). Consumers:

- `engine/render/src/shaders/v_peraxis_scatter.glsl:211,217-218` — calls
  `scatterConservativeDilation(…, kScatterDilateMarginPx, …)`.
- `engine/render/src/shaders/f_peraxis_scatter.glsl` — margin-vs-interior
  classification + margin depth bias / yield gradient.
- `engine/render/src/shaders/metal/peraxis_scatter.metal:194,198-200` — Metal
  mirror of the vertex call.
- `engine/render/src/shaders/ir_iso_common.glsl:691,701,717,723,738,768` +
  `…/metal/ir_iso_common.metal:636,642,650,654,660,678` — the definitions
  (all six mirror the GLSL; line 663 is a comment, not a symbol).

No other shader, system, or CPU caller references these symbols. The detached
regime (`kScatterDetachedPitchFraction`) and camera regime
(`kScatterDilateMarginPx` floor) both route through the same
`scatterConservativeDilation`, so both are covered by the same retirement.

## Acceptance criteria (epic-level)

- Coarse-cube (3³ / 12³) near-cardinal poses render **crisp corners (no spikes)
  AND solid foreshortened faces (no dashing)** — without the manual dilation
  margin deciding coverage.
- Both backends (Metal + OpenGL) at parity.
- Temporally stable under the #1922 jitter harness (validated in C3, once #1922
  is on master).
- No regression on the epic #1881 rotated-solidity baseline; **cardinal fast
  path byte-identical**.
- The manual margin/miter/yield tower is retired (or reduced to a documented
  minimal visit-bound); `engine/render/CLAUDE.md` drift note updated.

## Gotchas

- **Shared shader with #1920 (SDF jitter).** `ir_iso_common.{glsl,metal}` holds
  both `scatterConservativeDilation` (this epic) and `findSurfaceDepth`
  (#1920/#1925, currently design-blocked on #1922). They are different functions,
  but per #1881's one-at-a-time rule, coordinate so this epic's children and the
  #1920 work don't land overlapping edits to the same file simultaneously.
- **R32I / depth co-sort is a hard per-pixel write** — analytic coverage feeds a
  **hard threshold + discard** (or equivalent), not alpha-blended partial
  coverage, because the distance/depth metric must stay a single value that
  co-sorts with the SDF path (`c_shapes_to_trixel.glsl` #1370 metric). Do not
  desync the scatter depth from that metric.
- **Cardinal fast path must stay byte-identical** — the cardinal scatter is the
  zero-residual case; the analytic term must collapse to the current exact
  footprint there (the spikes/dashing are an inter-cardinal-only artifact).
- **Bind-point budget is full (0–30).** The analytic model is chosen partly
  *because* it needs no new GPU buffer/texture; if an implementation reaches for
  one, that is a design smell — re-derive from varyings + the existing per-axis
  canvas reads instead (`engine/render/CLAUDE.md` §Gotchas).
- **#1922 dependency is real for C3 only.** C1/C2 validate the static defect
  (spikes/dashing) with the existing `perf-grid-rotate-sweep`; only the temporal-
  stability gate needs #1922 (PR #1928, approved/landing).
- **`backend-parity` skill for C2** — the GL and Metal scatter shaders are hand-
  mirrored; use the skill's audit so the analytic logic doesn't drift.

## References

- #1883 / #1917 — the corner-spike no-local-fix proof; #1917 routes the root fix
  to this epic and documents the accepted drift C3 removes.
- #1907 (merged) — interim dilation iters (coverage bands + face-alignment seams).
- epic #1881 — jitter/rotated-solidity epic; #1922 (PR #1928) the validation
  harness; #1882 / `perf-grid-rotate-sweep` the solidity gate.
- #935 / #937 — rasterization-quality epics.
- `engine/render/CLAUDE.md` — convex-corner drift note (#1917) + bind-point
  budget gotcha.

## Steward ledger

reconciled-through: PR #2013 merge (2026-07-13)
proposal-pending: none

### Children
| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #1937 | merged | #2013 | plan (stale header — see note) | 2026-07-13 |
| #1938 | open | — | plan (amended A1) | 2026-07-13 |
| #1939 | open | — | plan (amended A1) | 2026-07-13 |

### Decisions
- D1 (2026-06-21): approach picked = portable analytic edge-aware coverage; HW
  conservative raster (no Metal API; GL extension non-portable on the WSL 4.5
  floor) and MSAA (integer R32I/depth resolve + 4–8× memory) ruled out by the
  source-verified capability audit.
- D2 (2026-07-13): C1 lead backend = **Metal** (#1937, PR #2013); C2 = **GL**
  parity (#1938). The GL→Metal lead order was flipped after planning — the child
  titles were updated but the plan files were not; #1938/#1939 amended (A1) to
  match. The approach (D1) is unchanged — this is a sequencing flip, not an
  approach change. source: PR #2013 (metal shaders) + updated child titles +
  in-code note `metal/peraxis_scatter.metal:282-283`.

### Events
- 2026-06-21: filed via file-epic (planning of #1933).
- 2026-07-13: **#1937 (C1) merged via PR #2013** — analytic edge-aware coverage
  landed on the **Metal** backend (`metal/ir_iso_common.metal`,
  `metal/peraxis_scatter.metal`); macOS references refreshed. Umbrella checklist
  ticked. The GL side (`v_peraxis_scatter.glsl`, `ir_iso_common.glsl`) is still
  on the old `scatterConservativeDilation` margin model — that is #1938's port
  target. No scope drift vs #1937's (backend-flipped) plan intent.
- 2026-07-13: **Drift note (record-only):** the merged child #1937's own plan
  file (`issue-1937.md`) header/scope still reads "GL backend (C1)" — a
  pre-merge title-flip artifact. #1937 is closed (no worker resumes it), so its
  plan is left as-is; the authoritative issue title + PR #2013 record the truth.
  The umbrella checklist descriptions were realigned to the current issue titles
  during the tick (heal-shape).

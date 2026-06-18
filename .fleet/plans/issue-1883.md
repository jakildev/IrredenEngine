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

## Session progress / RESUME (2026-06-18)

Worked in a dedicated architect session. Branch `claude/1883-per-axis-scatter-seams`
(PR #1907, WIP, stacked on `claude/1882-cardinal-gather-coverage`/#1885).

**Iteration 1 — DONE (commit a48bec0f).** Per-axis continuous dilation margin: the
margin now lives inside `scatterConservativeDilation`, derived per-edge as
`max(kScatterDilateMarginPx, 0.5*|n|)` from each axis's own on-screen perpendicular
extent; the miter is generalized to unequal per-edge margins (solve
`[e1;e2]·δ=(marginU,marginV)`, reduces to the #1538 equal-margin miter). The old
`suLen/svLen/degenSin/adaptiveMargin` block is removed from both callers. GLSL +
Metal in parity. **Result (macOS/Metal sweep zoom tier): near-cardinal seam perim
43 → 22 (= clean card000 baseline ~21), coverage 1.0, silhouette dashing gone.**

**Iteration 2 — DONE: the doubled sliver at the top↔side shared edge.**
Penetration-scaled margin yield (`kScatterMarginYieldGradScale = 3`) in the per-axis
scatter. **Empirical isolation overturned the planned root cause:** a margin-floor
diagnostic (set the per-axis margin to its `minMarginPx` floor) made the doubled
sliver vanish *entirely*, proving the sliver was **iter-1's own `0.5*|n|` margin
over-painting across the ridge**, NOT the rounding split (bullet 2) — flooring also
brought the silhouette dashing back (perim 22→42), so the floor is not a fix. Root
cause is bullet 3 (no deterministic ridge owner) at scale: on a foreshortened face
iter-1's margin reaches a cell-deep fraction, so the over-grown TOP-face margin
EXTRAPOLATES its plane deep enough to beat the LEFT face's *exact* footprint along a
~16px band below the ridge (sampled: exact TOP color (93,111,130) bleeding past the
ridge onto LEFT (88,106,124)). The flat `kScatterMarginDepthBiasKey` (0.25) only
breaks sub-pixel same-plane ties — far too small once the margin buys a real
plane-depth advantage. **Fix:** scale the margin's depth yield by its own
extrapolation excursion (penetration past the exact `[0,1]^2` footprint × the
per-axis screen-depth gradient `|kU|/|kV|`), folded into a `vMarginYieldGrad{U,V}`
varying. A sub-pixel gap-fill barely yields (still wins background / cross-cube
silhouette overlaps); a cell-deep margin yields hard (loses the ridge to the
neighbor's exact footprint). GLSL + Metal in parity; per-axis-only, so the cardinal
fast path is untouched. **Result (macOS/Metal sweep):** doubled sliver collapses to a
clean single ridge (column samples TOP→LEFT in one step; before/after + img_diff in
`/tmp/iter2_ridge_before_after.png`), near-cardinal perim stays at the iter-1 ~22
(NOT the floored ~42), coverage 1.0 across the whole residual band, CARDINAL CHECK
PASS (path=single). Rounding unification (bullet 2) was deliberately NOT applied —
it does not touch the visible defect and would perturb the delicate cast/receive
shadow path; left as a possible future consistency hardening.

**Env traps:** (1) macOS builds METAL only — edit `metal/*.metal`, validate, then
mirror to the GLSL twin before committing (Linux validates GLSL via cross-host
smoke). (2) `setCameraVisualYaw` takes DEGREES (`--yaw 84` = a near-90 per-axis
pose). (3) The canonical per-axis-seam capture is the `--yaw-ramp --yaw-ramp-crops`
ROI pass at `--grid-size 12 --zoom 4` (e.g. `cd build/creations/demos/perf_grid &&
./IRPerfGrid --mode dense --yaw-ramp --no-overlay --auto-screenshot 80 --grid-size 12
--zoom 4 --yaw-ramp-crops`; near90_m06 = the 84° pose). NOTE: `--yaw N` is IGNORED
when `--auto-screenshot` runs the shot table — kShots/yaw-ramp override per-shot yaw,
so there is no single-pose static-yaw capture path; use the ramp's near-cardinal
crop instead. Pixel-sample a column across the ridge (PIL) to read seam structure —
the aggregate perim does NOT show the doubled sliver. (4) Worktree guard blocks writing
to `/tmp` and `~/.fleet/` — keep temp commit-msg/PR-body files inside the worktree.

**Claims:** #1883 claimed (`fleet:claim-mac-opus-architect`). #1884 worker-protected
by the Blocked-by gate while #1907 is WIP; formally claim it
(`fleet-claim claim 1884 opus-architect --stackable-on <#1907 url>`) once #1907
leaves `fleet:wip`. #1882 fix+harness = PR #1885, in fleet review (don't re-touch).

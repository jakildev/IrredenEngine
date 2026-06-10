# Plan: per-axis store occlusion key — decouple yawed winner from un-yawed recovery (#1457)

- **Issue:** #1457   **Model:** opus   **Date:** 2026-06-09 (architect RE-plan, supersedes the 06-07 plan)
- **Repro:** CONFIRMED live (macOS/Metal): `fleet-run IRShapeDebug --spin-yaw 30 --auto-screenshot 8 --zoom 8 --depth-color`, shot 2 (yaw 45°) + shot 4 (yaw 135°) — voxel-pool per-axis shape shows scrambled blocky depth (front/back confusion); SDF shape is a clean gradient.

## ⚠️ The original lead is DISPROVEN — do not repeat it
The 06-07 plan said "make the scatter composite depth per-fragment instead of flat-per-face."
PR #1601 implemented exactly that and verified it is **byte-identical to master** (`img_diff
drift=0`, `cmp` identical). The scatter composite depth is **not** the cause — forcing the scatter
fragment to magenta paints a solid coherent square over the scrambled region, so placement/tiling
is correct. The scramble is *which voxel is stored in each cell*, decided upstream at store time.
The 06-07 "instrument-first / follow the data" caveat fired; this re-plan follows it.

## Real root cause (re-diagnosed)
`engine/render/src/shaders/c_voxel_to_trixel_stage_1.glsl` (per-axis store, ~line 260), twin
`stage_2`, Metal twins. The per-cell occlusion winner is chosen by `atomicMin` on
`encodeDepthWithFace(microWorld.x+microWorld.y+microWorld.z, slot)` = **un-yawed** `x+y+z`. But the
canvas cell corresponds to a **yawed** screen position; the correct "nearest" along a face's fixed
axis is the **yawed** depth:
- X-faces: `depth_yaw ∝ coord·(cosθ − sinθ)` → coefficient `→ 0` at **θ = 45°** → rank-degenerate.
- Y-faces: `depth_yaw ∝ coord·(sinθ + cosθ)` → coefficient `→ 0` at **θ = 135°** → rank-degenerate.
Exactly the DoD's two worst poses (shots 2/4). Back voxels win `atomicMin` → speckled front/back.

## The coupling that blocks the obvious swap
The stored 30-bit value is **also** the scatter's origin-recovery key:
`v_peraxis_scatter.glsl` does `faceOriginFromInPlane(faceId, inPlane, rawDepth)`
(`ir_iso_common.glsl:538`), `third = rawDepth − inPlane.x − inPlane.y`. That recovery is exact,
trig-free, division-free — robust at all yaws — **because** `rawDepth` is the un-yawed `x+y+z`.
You cannot simply store yawed depth instead: recovering `third` from a yawed key needs
`third = key / (cosθ − sinθ)`, which is the **same division that goes singular at 45°**. So the two
roles genuinely need two different values; one 30-bit field can't serve both.

## Key derivation insight (makes this bounded)
Within a single cell the in-plane coords are **fixed**, so the in-plane terms of the yawed depth are
constant per cell → ordering by full yawed depth ≡ ordering by the **fixed-axis term**
`third·(cosθ−sinθ)` (X) / `third·(sinθ+cosθ)` (Y). So the occlusion sort key only needs a
quantized scalar monotonic in `third` with the yaw-sign — cheap to compute at store time.

## Approach (one) — repack a single 32-bit atomicMin word
Keep one 32-bit `atomicMin` per cell (no 64-bit atomics → preserves GLSL↔Metal parity).
Repack the word as **[ high bits: quantized yawed sort key | low bits: un-yawed recovery payload ]**:
- High bits = `quantize(yawed fixed-axis depth)` so `atomicMin` selects the **yawed** winner.
- Low bits = the un-yawed recovery payload (`rawDepth`/`third` + 2-bit `slot`), read at scatter so
  `faceOriginFromInPlane` stays exact/trig-free/division-free.
- **Derive the bit budget** (opus-tier closed form): bound `third`/`rawDepth` range from the world
  extent along the depth diagonal and the visible canvas extent; confirm the quantized yawed key has
  enough resolution to order adjacent voxels across the whole band. Document the budget.
- **Fallback if 32 bits is too tight:** a parallel winner-select buffer using the standard
  packed-key idiom (atomicMin the sort key into buffer A; the winning fragment writes its payload
  into buffer B keyed by the same packed value). Note the extra-buffer cost + parity surface. Pick
  the 32-bit repack first; only fall back if the bit budget genuinely doesn't close.

## Accepted residual (intrinsic — do NOT chase it)
At **exactly** θ = 45° (X) / 135° (Y) the fixed-axis coefficient is 0: the column is genuinely
rank-degenerate and **no scalar depth key can pick a winner** — this is a property of the iso
projection, not the store. Leave that knife-edge within the documented SDF-vs-voxel parity band
(`engine/render/CLAUDE.md` §"SDF vs voxel-pool parity"). DoD targets "clean across the band," not
"byte-clean at exactly 45°."

## Constraints (hard)
- **Cardinal byte-identity:** gate everything on `perAxisRoute != 0`; do not perturb the
  single-canvas helpers (`encodeDepthWithFace`/`effectiveTrixelSubdivisionScale`/`trixelFrameOffset`).
- **GLSL↔Metal parity in lockstep** — store shader + Metal twin in the same PR.
- **Every reader of the stored value stays consistent** with the new payload layout: scatter
  recovery (`v_peraxis_scatter`), AO (`c_compute_voxel_ao`), lighting (`c_lighting_to_trixel` via
  `perAxisCellToWorld3D`), sun-shadow (`c_compute_sun_shadow`), `c_resolve_per_axis_screen_depth` —
  each + Metal twin. Audit all before finalizing.

## Reconcile with siblings
- **#1458** (Blocked-by relationship: #1458 waits on this) reworks the SAME store to base/world
  resolution + fractional winner offset. This PR establishes the **decoupled occlusion model**
  (yawed winner / un-yawed recovery) that #1458 inherits — keep the payload layout extensible so
  #1458 can add the fractional offset without re-deriving the key.
- **#1469** is closed (subsumed by #1458).
- **#1431** cap stays as-is (this PR doesn't touch resolution).

## Definition of done
- `IRShapeDebug --spin-yaw 30 --depth-color` shots 2/4: voxel-pool shape renders clean across the
  band — no scrambled blocks except the documented exact-45°/135° knife-edge.
- Off-cardinal yaw generally: no front/back confusion / z-fight on the voxel-pool shape.
- Cardinal yaw byte-identical (fast path untouched).
- GLSL + Metal parity; render-verify ROI crop (voxel shape, yaw 45°) committed.
- **Durable design:** add a "per-axis store occlusion model (yawed winner / un-yawed recovery)"
  section to `docs/design/per-axis-trixel-canvas-rotation.md` — this is an engine-level invariant,
  not just task-local. Note the 32-bit packing layout + the accepted knife-edge.

## Ownership
Discard PR #1601's no-op diff; start a fresh branch. Root-cause-AND-fix in one PR. `[opus]`.

---

## Architect RE-plan v3 (design-unblock, 2026-06-09) — PR #1625: store-key lead ALSO disproven

The worker implemented the v2 re-plan faithfully (repacked atomicMin word, yawed sort key /
un-yawed recovery payload) and **measured it byte-identical to master** at the worst-case pose
(yaw 67.5°, X coeff −0.54). The collision-free argument is accepted: the per-axis face-local
store holds exactly ONE camera-visible exposed face-voxel per cell (keyed by its two in-plane
world coords), so the atomicMin "winner" is uncontested and ANY store-time sort key is a no-op.
The v2 root-cause section above is therefore WRONG — superseded by this entry.

**Two faithful implementations have now been disproven byte-identical (#1601 composite-depth,
#1625 store-key). New hard rule for this issue: NO third fix implementation lands until the
failing mechanism is directly visualized.** Discard PR #1625's diff (commit `f2b37ed3` stays
referenced in the thread as evidence); reset the branch.

### What IS established (do not re-litigate)
- Per-axis scatter placement/tiling is correct (forced-magenta probe → coherent solid silhouette).
- The scramble = the WRONG voxel's authored depth-color per screen pixel, correct silhouette.
- The per-axis store is collision-free → store-time keys cannot matter.
- Remaining suspect: the **cross-axis framebuffer composite** — the three per-axis canvases
  (X/Y/Z visible surfaces) scatter their face quads and depth-test against each other per screen
  pixel via the scatter `vDepth`. Hypothesis: the three faces' flat per-face yawed depths
  interleave instead of cleanly separating → pixels alternate between X/Y/Z faces → block scramble.

### Mandatory instrumentation (in order; post results on the PR before any fix)
1. **Axis-canvas-ID visualization**: a debug mode (flag- or compile-gated, mirroring
   `--depth-color`) that colors each framebuffer pixel by WHICH axis canvas won the depth test
   (X=red, Y=green, Z=blue). In the scrambled region:
   - Interleaved IDs → cross-axis composite CONFIRMED as the locus.
   - Coherent single ID → the scramble is INSIDE one canvas's scatter; go to (2).
2. **Recovered-origin field visualization** per cell (colors from `faceOriginFromInPlane` output)
   — rules the trig-free recovery in or out as the corruption point.

Keep the instrumentation in-tree behind its debug flag — this surface has now consumed three
diagnostic rounds; the tool stays.

### Evidence-contingent fixes (pre-authorized — worker picks per the evidence, no re-escalation needed)
- **Cross-axis interleave confirmed** → make all three scatters emit ONE shared exact depth key:
  the true yawed camera-space depth of the **recovered voxel origin**. Recovery is exact and
  trig-free per fragment, so each canvas can compute the identical world-space depth for the voxel
  it recovered; one shared helper in `ir_iso_common.{glsl,metal}` used verbatim by all three
  scatters (the formula must be literally shared, not three copies). Note: #1601 does NOT disprove
  this — #1601 changed interpolation granularity of the EXISTING per-face key, not the ordering
  function across canvases.
- **Single-canvas scramble** → follow the recovered-origin evidence (scatter addressing/recovery
  bug in that axis); post findings before fixing.

### Re-escalate ONLY for the structural call
If the evidence shows no scalar cross-axis depth key can order the three faces (a genuine
forward-scatter-under-yaw limit, mirroring why detached SO(3) moved to re-voxelize in #1560),
retiring forward-scatter for camera yaw is an architect+human decision — swap back to
`fleet:design-blocked` with the visualizations attached. Do not make that call unilaterally.

### Definition of done (unchanged in substance — the artifact, not the mechanism)
- `IRShapeDebug --spin-yaw 30 --depth-color` shots 2/4: voxel-pool shape clean across the band —
  no scrambled blocks except the documented exact-45°/135° knife-edge.
- Off-cardinal yaw generally: no front/back confusion on the voxel-pool shape.
- Cardinal yaw byte-identical. GLSL + Metal in lockstep. Render-verify ROI crop committed.
- Instrumentation visualizations posted on the PR + the debug mode kept in-tree.
- Durable design note in `docs/design/per-axis-trixel-canvas-rotation.md`: per-axis store is
  collision-free (one face-voxel per cell) → occlusion is decided at the cross-axis composite,
  plus whatever the confirmed fix establishes.

Resume on PR #1625 (reset the branch first). Any heavy-tier worker can pick up from
`fleet:design-unblocked`.

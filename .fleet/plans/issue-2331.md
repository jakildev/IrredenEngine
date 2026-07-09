# Epic #2331 — per-axis store loses view-visible faces under residual yaw

Status: **approved + filed 2026-07-08** (umbrella #2331). Phase D1 folds the
durable parts into `per-axis-trixel-canvas-rotation.md`.
Author: opus-architect.

## Symptom report (human, IRPerfGrid)

1. The solid cube (default `voxel_set` scene, traveling wave) looks **hollow**
   when rotating under camera Z-yaw.
2. Wave crests near the top are **visibly missing ("culled") under yaw, but
   not at the origin camera angle** — including small rotations **within the
   same quadrant**.
3. Long-standing **banding** on IRPerfGrid under yaw.
4. Suspicion that "distance-winning voxels don't change under yaw within a
   quadrant" does not hold, and that nothing re-orients distances/culling in
   new quadrants.

## Root cause

While rotating (`residualYaw != 0`), the main canvas renders through the
per-axis store + forward scatter (#1309/#1310). The store
(`c_voxel_to_trixel_stage_1.{glsl,metal}`, `perAxisRoute != 0` branch):

- keys each face by its **un-yawed (cardinal) iso pixel**
  `perAxisBase + pos3DtoPos2DIso(facePos)` — a 2D projection whose kernel is
  the world `(1,1,1)` direction;
- elects **one winner per cell** by `imageAtomicMin` on the **un-yawed** depth
  `pos3DtoDistance(facePos) = x+y+z`.

The scatter (`v_peraxis_scatter`) can only draw faces that survived the store.
Therefore **the rendered face set while rotating is exactly the
CARDINAL-visible set** — but the view needs the **VIEW-visible set** at
`visualYaw = cardinal + residual`. Any two faces separated by `t·(1,1,1)`
("a coset pair") share one store cell; only one survives. Under residual yaw
θ their screen positions separate by

```
Δiso = t · (−2·sinθ, 2 − 2·cosθ)      // pos3DtoPos2DIsoYawed((1,1,1)·t, θ)
```

≈ 0.35 iso px per unit `t` at 10°, ≈ 1 px per unit at 30°. So a face occluded
at the cardinal but revealed by even a small residual yaw is **absent** — there
is nothing in the G-buffer to scatter. The design doc's claim
(`per-axis-trixel-canvas-rotation.md` §"Mechanism chosen": "two faces that are
screen-separated at the live yaw never share a cell") conflates
cardinal-screen-unique with live-yaw-screen-unique; it is false for any content
with ≥ 2 view-visible faces in one `(1,1,1)` coset.

### Secondary defect — winner metric never re-orients per quadrant

The election metric `x+y+z` is the **yaw-0** depth at every quadrant. Along
the live view kernel, coset depth order scales with `(2·cos(visualYaw) + 1)`:

- `> 0` for full yaw within ±120° → winner agrees with the view;
- `= 0` at 120°/240° → degenerate (ties);
- `< 0` for full yaw in (120°, 240°) → **inverted**: the store keeps the
  view-FARTHEST member of every colliding coset and drops the visible one.

(The *composite* depth among survivors was already fixed to the yawed metric —
#1370 — but ordering survivors cannot resurrect dropped faces.)

### Why the #1880/#1881 harness never caught this

`perf-grid-rotate-sweep` runs `--mode dense` — one solid axis-aligned cube.
Its exposed faces per axis lie on a single flat plane, and a plane contains no
two points differing by `t·(1,1,1)` → **zero coset collisions** → per-axis
poses scored 0.95–0.99 coverage. The defect requires concave or multi-body
content: the default IRPerfGrid wave scene is a worst case — per-cell
single-voxel sets (all six faces "exposed", no cross-set occupancy) plus a
wave whose **phase advances with `(x+y+z)`**, i.e. the surface repeats along
the store's kernel direction, maximizing collisions.

### Symptom → cause map

| Symptom | Cause |
|---|---|
| Wave gaps appear "a bit" into a quadrant | Coset losers revealed by residual yaw; nothing to scatter |
| "Hollow" cube | Per-cell wave scene: every block face is corrugated → coset losses on all visible faces; gaps read as holes into background |
| Banding under yaw | Collision sets are periodic in `x+y+z` (wave phase rides the kernel) → loser gaps cluster in constant-depth diagonal bands |
| Worse / inverted in far quadrants | `x+y+z` election metric sign-inverts past 120° full yaw |
| Waves "culled" at top | Same store loss (crests are local z-maxima = local screen-top); **not** the iso cull — verified: compact cull projects with full `visualYaw` + margin, chunk bounds are conservative 8-corner yawed projections (#1439), cull viewport reads `getEffectiveCameraIso()` |

### Explicitly ruled out (verified in code)

- `c_voxel_visibility_compact` iso-rect cull: yaw-aware (full-`visualYaw`
  projection + margin 2 when rotating; cardinal-rotated positions otherwise).
- `buildChunkVisibilityMask` / `rebuildChunkBounds`: conservative world-AABB
  8-corner projection under continuous yaw; cardinal path rotates positions.
- Cull viewport camera source: `getEffectiveCameraIso()` (pivot-corrected).
- Composite depth among survivors: yawed planar metric (#1457/#1370).
- Fully-interior compact drop: all-six-faces-occluded only — yaw-independent.

### Related open work (not duplicated by this plan)

- Epic #1881 defect 1 — cardinal **gather** coverage loss at 90/180/270
  (per-axis path inactive there). Separate path, stays in #1881.
- #2207 / PR #2296 + #2157 — silhouette-riser polarity for ROTATED content in
  the per-axis store (2-bit slot cannot express opposite-polarity faces).
  Same store, different loss class. **Synergy note:** overflow entries below
  carry an explicit `faceId`, so the overflow lane can natively represent
  opposite-polarity riser faces — a potential landing zone for #2207's
  deferred per-axis dual emit.
- #2255 — per-axis deterministic winner election (`resolveMode`,
  `PerAxisWinnerScratch`). Its two-pass store pattern + transient binding-28
  reuse is the implementation template for the passes below.

## Fix design — view-visibility overflow (additive, rotating-only)

Keep the cardinal-keyed store exactly as-is: it is crack-free, its
`(cell, rawDepth) → world` recovery is exact at every yaw, and its winners are
correct for the dominant convex case. Add a bounded **overflow lane** that
carries exactly the missing set `viewVisible ∖ cardinalWinners`:

1. **Pass V — view mask** (new dispatch between the store and stage 2, gated
   `perAxisRoute != 0`): every per-axis face `atomicMin`s its quantized
   **yawed** depth (`pos3DtoDistanceYawed`) into an R32I scratch keyed by
   `roundHalfUp(pos3DtoPos2DIsoYawed(facePos, visualYaw))` (screen/trixel
   resolution). Pure filter — never read for recovery, so no invertibility
   constraint and the 120°/240° singularity is irrelevant.
2. **Pass O — overflow append**: re-run the same face geometry; a face
   appends `{voxelIndex, faceId}` (packed) to an overflow SSBO iff
   (a) its yawed depth is within ε of its view-mask cell winner
   (view-visible; ε ≈ one quantization step to absorb ties/rounding — slight
   over-emit is safe, the framebuffer depth test cleans up), AND
   (b) it is NOT its cardinal store cell's winner (encoded key ≠ stored
   value — the same match test `resolveWinnerTap` already does).
   Atomic counter + hard cap (size ~ canvas-cell count / 4); on cap overflow,
   drop + one-shot CPU warn with the dropped count (never silent).
3. **Scatter**: after the per-cell instanced draw, a second instanced draw
   over the overflow list. The vertex shader reads `positions[]`, `voxels[]`,
   `entityIds[]` by `voxelIndex` (all already bound), builds the identical
   deformed face-quad footprint (`pos3DtoPos2DIsoYawed` corners) and yawed
   planar depth. No recovery inverse involved.
4. **Lighting for overflow faces**: per-axis cells are lit at the canvas
   before scatter (T4); overflow faces have no cell, so their fragment path
   samples world lighting inline — sun-shadow cascade + light volume at the
   face's world position + Lambert, `AO = 1.0` (mirrors
   `c_lighting_to_trixel`'s world sample). Accepted drift: revealed slivers
   carry no screen-space AO.

**Size bound:** the view-mask filter caps overflow at ≤ view-visible faces ≈
O(screen cells), independent of interior content (matters for `voxel_set`
mode where per-cell sets make every interior voxel "exposed").

**Cost:** two extra bounded dispatches over per-axis faces + one bounded
instanced draw, **only while rotating**. Cardinal fast path byte-identical
(all gating on `perAxisRoute != 0` / `residualYaw != 0`).

**Bind-point budget (HARD constraint — budget 0–30 is full):** no new
permanent binding. View mask + overflow list + counter must ride transient
`bindRange`/`bindBase` reuse of bindings that are dead during the per-axis
dispatches — binding 28 (`kBufferIndex_PerAxisResolveScratch`) is the proven
precedent (#1435/#2255); the first implementation step is to map the
per-axis-window dead set and pick the second slot. If no second slot is dead,
pack view mask + overflow into one buffer at aligned offsets.

**Backends:** GLSL + MSL twins for every kernel (backend-parity rule). The
#1640 foreign-R32I caveat does not apply (main-canvas-owned scratch, same
in-tick pattern as #2255).

### Optional follow-up (perf, not correctness)

Switch the cardinal store's election metric to the yawed depth (decoupling
sort key from recovery payload via the winner-id indirection #2255 already
introduced). This shrinks the overflow near the 180° quadrant (where the
inverted metric currently maximizes it) but is unnecessary for correctness
once the overflow lane exists. Decide on measured overflow counts per
quadrant from V1.

## Phases (file-epic children, serialized — shared surface)

Filed: V1 = #2332, C1 = #2333, C2 = #2334, D1 = #2335.

1. **V1 — wave-scene rotation harness (validation first).** Deterministic
   wave pose (`--wave-freeze` or equivalent phase pin) + extend
   `perf-grid-rotate-sweep` with the default wave scene; hole-ratio /
   coverage per pose across a residual sweep AND all four quadrants. Capture
   the red baseline that #1880's dense-cube sweep structurally cannot see.
2. **C1 — view mask + overflow + scatter draw.** Passes V/O, overflow
   instanced draw rendering **albedo-only** (unlit slivers beat missing
   geometry; lighting is C2). Both backends. Gate: V1 goes green at
   quadrant-0 residuals; cardinal md5 A/B unchanged.
3. **C2 — overflow lighting + far-quadrant polish.** Light overflow entries
   in the per-axis lighting stage (sun + volume + Lambert, AO = 1, rewrite
   entry color in place); quadrant 1–3 sweep green; ROI crop pairs vs the
   cardinal pose; measure overflow counts + GPU delta (#2281 attribution
   rows); decide the optional yawed-election follow-up.
4. **D1 — docs.** Amend `per-axis-trixel-canvas-rotation.md`: supersede the
   stale §"Per-axis store occlusion model — engine invariant (#1457)"
   ("store is collision-free / winner uncontested" — written for the retired
   in-plane key) with the two-set model (cardinal-visible ⊉ view-visible)
   and the overflow contract. Update `engine/render/CLAUDE.md` pipeline note.

## Epic closing criteria

- Wave-scene rotate sweep: coverage ≥ 0.99 / hole-ratio parity with the
  cardinal pose at every swept residual and in all four quadrants, Metal AND
  OpenGL.
- Dense-cube sweep (#1880) unregressed.
- Cardinal / yaw-0 path byte-identical (md5 A/B).
- Rotating-frame GPU delta bounded and attributed (per-axis rows, #2281);
  overflow cap never hit on the demo scenes (warn counter = 0).

## Steward ledger

reconciled-through: 2026-07-08
proposal-pending: none

### Children
| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #2332 | open | — | plan | 2026-07-08 |
| #2333 | open | — | plan | 2026-07-08 |
| #2334 | open | — | plan | 2026-07-08 |
| #2335 | open | — | plan | 2026-07-08 |

### Decisions
<!-- entries: D<n> (<YYYY-MM-DD>): <decision> — source: <link>  (numbered scheme per epic-steward-protocol.md §Decisions; escalation rules reference decisions by D-id) -->

### Events
- 2026-07-08: filed via file-epic

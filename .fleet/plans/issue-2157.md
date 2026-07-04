# Plan: rotated GRID/re-voxelize cubes — residual non-cardinal gaps + dark riser shading after the silhouette-riser fix

- **Issue:** #2157
- **Model:** opus
- **Blocked by:** #2162 (the silhouette-riser fix — this plan investigates/extends the same `c_voxel_to_trixel_stage_{1,2}` shaders; all measurement must be on the post-#2162-merge base)

## Context

#2162 added silhouette-riser face emission for rotated voxel content (emit the
exposed opposite-polarity face the convex visible-triplet drops, gated to
rotated content via `reVoxelize` / `VoxelReserved::kRotatedEmit`). It collapsed
the dominant gaps: worst cardinal 270° went 7.98% -> 0.29%, and axis-aligned
content stayed byte-identical at every camera direction. Two residuals remain,
each a *different* mechanism from the triplet-polarity gap #2162 fixed. This
plan root-causes the first (the only one whose home is genuinely undecided) and
routes the other two to their existing epics.

## Phase A — root-cause the non-cardinal residual (the core of this issue)

**Symptom:** on the frozen-pose camera sweep, cardinal directions are clean
(<=0.29%) but non-cardinal directions still show ~0.4-1.2% enclosed-black
(worst 1.22% at 112.5deg). Non-cardinal camera yaw is exactly where the main
canvas engages the **per-axis scatter route** (`perAxisRoute` 1/2/3).

**The fork to resolve** — is the residual:
- **(a) a flip x per-axis-store interaction** introduced by #2162 — the
  silhouette-riser flip runs before the `perAxisRoute` branch, so a flipped
  (opposite-polarity) face now reaches the per-axis store. `faceId ^ 1` keeps
  the same axis, so the `(faceId >> 1) != axis` store filter still routes it
  correctly, and `faceDeform_[slot]` is slot-(axis-)indexed so the deform still
  applies — but confirm the flipped face's **cardinal iso anchor / base-resolution
  store key** (#1458) lands on the opposite-face pixel it should, and that the
  forward-scatter composite covers it. If the flip mis-stores or under-covers the
  riser specifically on the per-axis route, that is in-scope and fixable here.
- **(b) the #1933 per-axis scatter coverage limitation** — foreshortened-face
  sub-pixel coverage loss, independent of the flip (present on rotated content
  pre-#2162 too). #2013 shipped Metal analytic edge coverage but the residual
  persists; if the riser faces simply inherit the same coverage shortfall, this
  is **not** #2157's to fix — re-point to #1933 / its open children (#1938 GL,
  #1939 cleanup) and close Phase A as "covered by #1933."

**Method (post-#2162 merge):**
1. `IRCanvasStress --only gridspin --frozen-pose {0.3,0.6,0.9} --sweep-yaw 0 6.2832 17 --auto-screenshot 30`; measure enclosed-black per direction (the comparator from the #2162 verification). Confirm the residual is at the per-axis (non-cardinal) indices across multiple poses.
2. Isolate (a) vs (b): re-capture with the flip temporarily gated off on the per-axis route only (add a `perAxisRoute == 0` term to the flip condition) — if the non-cardinal residual is unchanged, the flip isn't the cause -> (b); if it changes, the flip's per-axis store path is implicated -> (a).
3. Cross-check a non-rotated foreshortened solid at the same non-cardinal yaws: if it shows the same per-direction coverage shortfall, that confirms (b) (#1933).

**Decision gate:** (a) -> fix the per-axis riser store/coverage here. (b) -> re-point to #1933, close Phase A.

## Phase B — riser AO shading (cosmetic; likely folds into #2089/#1718)

The risers #2162 now emits read darker than the cube faces (near-grazing -> AO
darkens them), so the cube is solid but visibly banded. #2089 (tilt-aware AO
same-face resample) already handles GRID/re-voxelize staircase AO; extend its
same-face-neighbour logic to the silhouette-riser faces so they shade
consistently. Lower priority than A (no see-through, just shading). If the fix
is purely in the AO resample, prefer landing it under #2089/#1718.

## Phase C — perf audit (folds into #1808)

The flip emits the opposite-polarity (back) face for rotated content (gated, so
non-rotated content pays nothing). On dense rotated solids this is extra emits
that lose the depth `atomicMin`. Measure against the #1808 `dense_set` rotated
budget (`perf_grid` / `canvas_stress` rotated content): if material, gate the
flip more tightly (skip provably-occluded back risers) or accept and document.
Only act if the measurement shows a real regression.

## Acceptance criteria

- Phase A: the non-cardinal residual is root-caused with a one-line verdict
  ((a) fixed here, or (b) re-pointed to #1933 with the cross-check evidence).
  If (a): non-cardinal gap% drops to the cardinal level (<=~0.3%) on the
  frozen-pose sweep, axis-aligned stays byte-identical (img_diff drift 0).
- Phases B/C: either landed under their parent epics or explicitly deferred
  with the measurement that justifies it.
- GL + Metal parity; cross-host smoke cleared for any shader change.

## Dependencies

Blocked by #2162. Phase A is the core; B -> #2089/#1718, C -> #1808.
References: #1933 (per-axis scatter coverage), #2013 (analytic coverage),
#1458 (per-axis base-resolution store), #2089 (AO staircase), #1808 (perf),
#1881 (rotated-voxel correctness epic).

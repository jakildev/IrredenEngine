# Per-entity SO(3) depth/cull authority — open design question (#1397)

**Status:** open — needs an opus-architect call. This is an investigation
brief, not a chosen design. It maps the verified current state so the
architect can scope the work and decide direction; it deliberately does not
pick an approach.

#1397 is the "deeper half" of per-entity full SO(3), carved out of #1386.
#1386 (merged as PR #1398) fixed **which faces** a rotating entity shows;
#1397 is about **occlusion/depth correctness** once it rotates off-cardinal.
The issue and its plan (`~/.fleet/plans/issue-1397.md`) both defer the
approach with "design when unblocked — larger, may decompose."

## What #1386 / #1398 already solved (do not redo)

- **Per-entity visible triplet.** `IRMath::visibleTriplet(const vec4&
  rotation)` (`engine/math/include/irreden/ir_math.hpp:513`) returns the
  three camera-facing faces for an entity's own quaternion. The detached
  path calls it at `system_voxel_to_trixel.hpp:110-117`, so face *selection*
  is already orientation-correct (no longer hardcoded to {X_NEG,Y_NEG,Z_NEG}).
- **Exposed-mask is orientation-independent.** The per-voxel exposed-face
  bits are pure voxel-adjacency topology, set at pool build/mutate time and
  read at raster via `faceIsExposed` (`ir_iso_common.glsl`). A face is
  exposed iff its neighbour cell is empty — no orientation enters. So *all*
  potentially-visible faces' data is already render-available; nothing
  pre-culls to a fixed triplet's data.
- **Residual face deformation.** `faceDeformationMatrixSO3`
  (`system_voxel_to_trixel.hpp:122-124`) skews each emitted face's *shape*
  for the residual (post-octahedral-snap) rotation.

## The remaining gap (the actual subject of #1397)

The integer trixel raster computes per-voxel occlusion **depth** as the
iso-depth scalar `x + y + z` (`pos3DtoDistance`,
`c_voxel_to_trixel_stage_1.glsl:211,238`), written into the shared distance
buffer and used for the per-pixel depth test. For a detached entity the
camera yaw is zeroed (`rasterYaw_ = 0` → `cardinalIndex == 0`), so no
cardinal rotation is applied and the stored depth is the voxel's **model-space**
`x + y + z`.

That metric is the (1,1,1) iso-depth axis. It correctly orders occlusion only
at the 24 octahedral cube orientations the snap targets. Under the **residual**
(off-cardinal pitch/roll) rotation, `faceDeformationMatrixSO3` deforms each
face's silhouette but the depth scalar is **not** re-derived in the entity's
true orientation. So two voxels whose true camera-depth order flips under a
small pitch keep their snapped depth order — voxels that lost the depth test
at one orientation win it at another. That is the "pitch or roll reveals"
failure the issue describes: missing/incorrect occlusion at non-cardinal
orientations.

This is the same class of change the iso-depth-axis invariant doc
(`docs/design/iso-depth-axis-invariant.md`) costs as **"Rewrite"** for the
depth metric — here scoped to *entity* rotation rather than *camera*
rotation. That doc explicitly leaves the pitch/roll scope decision open
("#1076 — camera SO(3)… A/B decision still pending an opus-architect call").

## Why this is not a bounded single PR right now

1. **It is an invariant change.** A correct fix must give per-entity SO(3)
   entities a depth metric relative to the entity's own orientation, without
   weakening the camera Z-yaw iso-depth invariant (#1258) that the whole
   GRID render path relies on. That is an architecture call on a load-bearing
   invariant, not a local edit.
2. **The validation vehicles do not exist yet.** The acceptance criteria
   validate against **MAIN_CANVAS_SO3** entities and the **IRRotationCompare
   (#1301)** demo. `MAIN_CANVAS_SO3` is not in master (it is #1299 PR-A,
   currently `fleet:design-blocked` as PR #1408); #1301 is blocked on
   #1299 + #1300. The detached-only slice can be exercised in IRCanvasStress,
   but the depth-metric mechanism is shared with the main-canvas per-entity
   path (the `visibleTriplet` comment at `system_voxel_to_trixel.hpp:109`
   notes the resolver is "reused verbatim by the main-canvas per-entity path
   (#1299)").
3. **The scope is epic-sized.** Both the issue and plan say "larger, may
   itself decompose." A residual-aware depth metric touches the integer
   raster's depth contract, the distance-buffer semantics, and GLSL+Metal
   parity — plausibly several PRs.

## Open questions for the architect

1. **Depth model.** What replaces model-space `x+y+z` as the per-voxel
   occlusion depth for a per-entity SO(3) entity? Options to weigh: project
   each voxel onto the entity-rotated view-depth direction
   (`R⁻¹·(1,1,1)`-style, mirroring `visibleTriplet`) vs. a per-canvas
   depth-bias bake vs. accepting octahedral-snap depth + a residual
   correction term. Each has a different GLSL/Metal cost and parity surface.
2. **Invariant scope.** Should `iso-depth-axis-invariant.md` be amended to
   carve out per-entity (not camera) rotation explicitly, and what is the
   contract that keeps the camera Z-yaw GRID path byte-identical?
3. **Sequencing vs #1299.** Should #1397 wait on #1299 (MAIN_CANVAS_SO3
   position/emit mechanism) so the depth authority is designed once for both
   detached and main-canvas paths? The stated blocker (#1386) is merged, but
   the work is entangled with the design-blocked #1299. If yes, #1397's
   "Blocked by:" should gain #1299 (a human edit — workers don't re-scope
   issue bodies).
4. **Decomposition.** If this is an epic, what is the first shippable slice
   — detached-only depth correctness in IRCanvasStress, or hold until the
   main-canvas path lands?

## References

- Issue #1397; plan `~/.fleet/plans/issue-1397.md`.
- #1386 / PR #1398 (visible-triplet, merged); #1299 / PR #1408
  (MAIN_CANVAS_SO3, design-blocked); #1300, #1301 (blocked consumers);
  epic #1272; #1076 (camera SO(3) A/B, architect-pending).
- `docs/design/iso-depth-axis-invariant.md` — depth-metric cost map.
- `docs/design/voxel-face-rasterization.md` — visible-triplet × exposed-mask
  model.
- Call sites: `system_voxel_to_trixel.hpp:96-133`,
  `c_voxel_to_trixel_stage_1.glsl:112,211,230-238`,
  `ir_math.hpp:513` (`visibleTriplet`).

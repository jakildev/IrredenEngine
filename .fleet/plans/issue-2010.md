# Plan: render — re-voxelize SOLID cubes venetian gaps under rotation (confirmation spike → fix)

- **Issue:** #2010 (`fleet:task`, part of epic #1717; geometry-coverage prereq for the #1717 C-series)
- **Model:** opus
- **Date:** 2026-06-27
- **Architect:** opus-architect

## Status: confirmation spike FIRST

Root cause is **not yet proven** (cf. #1457 mis-diagnosis history). Do not patch
before an instrumented repro confirms the mechanism.

## Verified current state (2026-06-27 code read)

Revoxelization is already inverse-mapped — there are no classic forward-mapping
holes: DETACHED MODE 1 inverse path `c_revoxelize_detached.glsl:121-165`; GRID
inverse walk `system_rebuild_grid_voxels.hpp:420-444`. So the venetian gaps are
NOT missing occupancy; they are a **face-exposure-mask** defect on a solid that
is geometrically present.

## Leading hypothesis (to confirm)

`revoxDestCovered()` (`c_revoxelize_detached.glsl:89-97`) inverse-maps each dest
neighbor via `roundHalfUp(rotateByInverseQuat(...))` and returns false when it
lands outside the tight source AABB. On near-cardinal residual rotations the
round-to-cell toggles neighbor coverage on alternating rows → the six-neighbor
exposed mask (:152-159) flips row-by-row → venetian banding. Non-uniform shapes
amplify it (more edge voxels). Possible second contributor: CPU<->GPU mask
mismatch — CPU recompute `system_rebuild_detached_voxels.hpp:111-141` vs GPU
authorship `c_revoxelize_detached.glsl:144-161`, with the CPU color/active
upload SKIPPED when `revoxInverse==true` (`system_voxel_to_trixel.hpp:902`).

## Confirmation steps

1. Instrument the exposed mask: dump per-cell flags for a mid-bracket pose of a
   SOLID cube; check whether the banded rows correspond to flipped exposure bits
   (not missing occupancy). `--no-lighting` to isolate geometry from lighting.
2. A/B: widen the source-AABB neighbor query by 1 cell (or treat
   out-of-AABB-but-occupied-source as covered) and observe whether banding closes.
3. Check CPU vs GPU mask agreement on the identical pose.

## Then fix

Per whichever contributor confirms: correct the neighbor-coverage boundary /
rounding so exposure is rotation-stable, and/or reconcile CPU<->GPU mask
authorship. Land on BOTH backends.

## Files

c_revoxelize_detached.glsl:89-97,144-161; system_rebuild_detached_voxels.hpp:111-141;
system_voxel_to_trixel.hpp:902-919; face_occupancy.hpp:132-162; metal twins.

## Acceptance

canvas_stress `revox_coverage` / `revoxelize_solids_zoom` shots solid (no
venetian banding) across a full spin AND the non-uniform `--spin-shape` figure
(commit ad93066d) at 0/45/90; both backends; `--no-lighting` clean (geometry)
confirmed before re-checking lit.

## Reconciliation

Geometry-coverage **prerequisite** for the #1717 C-series (shadow consolidation
assumes a hole-free caster). Sibling of #1718 (sun-shadow rows; in flight PR
#1742 — that is the lighting facet, this is the geometry facet). Shares the
rotated-voxel path with epic #1881 (Child 1 cardinal gather is a different
defect; coordinate ir_iso_common edits).

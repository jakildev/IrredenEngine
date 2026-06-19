# Plan: render — cardinal-gather coverage loss at non-start cardinals

- **Issue:** #1882
- **Model:** opus
- **Date:** 2026-06-16 (re-scoped 2026-06-17, FINAL scope 2026-06-18)
- **Epic:** #1881 — see .fleet/plans/issue-1881.md for full context + baseline
- **Gated on:** PR #1880 (harness) on master
- **PR:** #1885

## Scope history (read this — the ticket flipped twice)

1. **Original:** "the single-canvas cardinal path drops ~⅓ of the cube at the
   non-start cardinals (π/2, π, 3π/2)." Correct symptom.
2. **Re-scope #1 (WRONG, superseded):** "premise is a misdiagnosis; the
   single-canvas path is clean at the cardinals; the loss is the per-axis path
   (#1883); make this a harness-methodology fix." This rested on a static-yaw
   test that used `--yaw <radians>` — but `setCameraVisualYaw` takes
   **degrees** (`ir_render.cpp:185`, `deg * π/180`), so `--yaw 1.5708` held the
   camera at **1.57°, not 90°**. All four "static cardinals" were within ~2° of
   0° (clean by construction); 90/180/270 were never exercised. The
   "misdiagnosis" verdict was itself the misdiagnosis.
3. **FINAL (this plan):** the original premise is **vindicated** — the
   single-canvas cardinal path really does lose coverage at 90°/180°. Root
   cause is a residual-yaw **gate mismatch**, fixed at the source. Plus the
   render-path-aware harness the investigation produced (it is what caught the
   degrees/radians error and measures the real path per pose).

## Root cause (gate mismatch)

Two predicates over the **same** `computeYawSplit(...).second` residual, with
different thresholds:

- per-axis **allocation** gate frees the textures at `|residual| <= 1e-4`
  (`per_axis_canvas.hpp`, `kResidualYawDeadband`).
- render **path-select** gate routes to the per-axis path at `residualYaw_ != 0`
  exactly (`system_voxel_to_trixel.hpp:734`; source `voxel_frame_data.hpp:198`).

A cardinal whose quat round-trip residual lands in `(0, 1e-4]` (90°, and 180°
via the `wrapYaw` `-π + 1e-4` clamp) has its per-axis textures **freed** by the
allocation gate while the render still **selects the per-axis path** → it reads
freed/empty textures → see-through coverage holes. 0° and 270° round-trip to
residual *exactly* 0 → both gates agree (single-canvas) → clean. That asymmetry
is the whole "270 is fine, 90/180 are broken" signature.

## Fix (single source of truth)

Deadband the residual **at its one source** so every consumer agrees by
construction. `IRPrefab::Camera::computeYawSplit` now snaps `residualYaw` to
exactly 0 within `kResidualYawDeadband` (the constant moved into `camera.hpp`
next to the split). Consequences:

- allocation gate (`per_axis_canvas.hpp`) and render path-select gate
  (`system_voxel_to_trixel.hpp:734`) both reduce to `residual != 0` over the
  same deadbanded value — they cannot disagree.
- the GPU `FrameDataVoxelToCanvas.residualYaw_` and the sun-shadow bake's frame
  patch inherit the same snapped value (both go through `computeYawSplit`).
- **byte-identical** for visible rotation (`|residual| > deadband`) and for an
  exact cardinal (`residual` already 0). Only the broken `(0, 1e-4]` dead zone
  changes — it now takes the clean single-canvas cardinal path, which is what a
  settled cardinal should do.

Files: `engine/prefabs/irreden/render/camera.hpp`,
`engine/prefabs/irreden/render/per_axis_canvas.hpp`,
`engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp` (doc).

## Harness (the second deliverable, already on the branch)

Render-path-aware tiers in `creations/demos/perf_grid/main.cpp` +
`scripts/dev/perf-grid-rotate-sweep`:
- cardinal-isolation tier — settled exact cardinals; demo MEASURES which path
  drew each pose (`C_PerAxisTrixelCanvases::isAllocated`) and logs `RAMP-POSE`;
  the sweep joins the path label to the scored frame so a "cardinal" row is
  unambiguous (this is what kills the misdiagnosis class of error).
- near-cardinal residual tier — dense ±1..10° sampling where the per-axis seams
  / bands peak (#1883's signal).
- zoom tier — small cube, high zoom, ROI crops for the fine seams.

## Acceptance criteria

- `bash scripts/dev/perf-grid-rotate-sweep build dense 60`: all four settled
  cardinals read `path=single` AND coverage >= 0.99 (90/180 were ~0.84 before).
- No regression at yaw 0 or at any genuinely-rotating per-axis pose
  (`|residual| > deadband` unchanged → byte-identical).
- Holds on BOTH Metal (macOS) and OpenGL (Linux).
- Before/after cardinal numbers in the PR.

## Gotchas

- The fix is the per-axis/single-canvas SELECTION boundary — cross-check the
  smooth-yaw path (#1308/#1310): snapping is a no-op above the deadband, so
  visible rotation is untouched; verify the ramp's rotating poses are unchanged.
- Cardinal-0 fast path must stay byte-identical (residual already 0 → no-op).
- The cardinal coverage HOLES are this ticket; the per-axis **band** coverage
  loss at genuinely-rotating poses + the **face seams** stay #1883.

## Verification

macOS: `bash scripts/dev/perf-grid-rotate-sweep build dense 60` (check the
`path` + coverage columns at the four cardinals). Linux: `fleet-build IRPerfGrid`
+ same sweep. Cross-host `IRShapeDebug --auto-screenshot` smoke.

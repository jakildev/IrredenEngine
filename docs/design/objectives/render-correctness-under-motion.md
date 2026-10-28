# Objective: rendering stays correct and temporally stable under motion

**Status:** active

## Outcome
Camera motion (yaw / zoom / pan) and entity rotation produce artifact-free,
temporally stable output, and every stability claim is machine-gated — a
regression in crawl, cull, or rotation correctness fails a harness, not an
eyeball.

## Done means
- [ ] All 8 SDF shapes pass the jitter gate (`MAX_JITTER=4.0`) in
  `shape-rotate-jitter-sweep` on `origin/master` — today 2/8 fail on the SDF
  path: the curved-SDF depth-band family (curved_panel 14.25, ellipsoid 7.90 —
  #1920).
- [ ] The voxel-path per-shape jitter table is gated (captured 2026-10-27 in
  `docs/design/jitter-validation-harness.md`, all 8 passing; the gate waits on
  the table being stable).
- [ ] An OpenGL/Linux jitter baseline exists and the gate runs on both
  backends, not only Metal/macOS.
- [ ] Picking works during rotation — the per-axis scatter path writes the
  hovered-id SSBO (`docs/design/per-axis-trixel-canvas-rotation.md`).
- [ ] Per-axis rotating voxels cast into the shared sun map again without
  the self-occlusion artifact (#1435); scatter-quad conservative-coverage
  waffle resolved (#1494).
- [x] Cull-freeze validation stays green: yaw-time culling never drops
  on-screen content (`IRShapeDebug --cull-validate`) — macOS/Metal on
  2026-10-27: two consecutive 12/12 PASS runs, with all five state-guard arms
  PASS.

## Non-goals
Frame-rate / perf targets (tracked by the perf gate, not this objective);
LOD strategy; new projection modes.

## Current state
The measurement culture exists and is the strength: the jitter harness
(`docs/design/jitter-validation-harness.md`, `scripts/render-jitter-metric.py`)
pins temporal second-difference under a 1°/step 360° sweep; the cull harness
(`docs/design/cull-validation-harness.md`) proves yaw culling against a
frozen wide viewport; `render-verify` / `light-verify` gate stills. Committed
baselines: the pivot-pinned 8-shape table re-measured 2026-10-27 on
`ee85797c0` (`docs/design/jitter-validation-harness.md` § Baseline) passes
14 of 16 SDF + voxel runs, with ellipsoid and curved_panel failing on the SDF
path; canvas_stress passes 1.19. The open work is the curved-SDF depth-band
crawl, gating the voxel-path table, the GL baseline, and the rotation
residuals (picking, cast shadows, waffle).

## Progress ledger
| Date | Epic / issue | Delta |
|---|---|---|
| 2026-07-20 | — | objective seeded |

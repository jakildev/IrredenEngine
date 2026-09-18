# Objective: a million simple resident entities render at 60 fps under continuous camera motion

**Status:** active

## Outcome
A world holding one million simple resident voxel entities renders at a
sustained 60 fps at a fixed output resolution while the camera yaws and
zooms continuously, with representative lighting and shadows on, and with
the engine's identity intact: voxel geometry reconstructed as trixel faces,
canvases distorted for rotation, geometry-aware lighting, no meshes. Cost
tracks useful screen coverage rather than resident population, rotation
and zoom are close to cardinal parity, and simulation cadence is a separate
decision from render visibility.

## Done means
- [ ] `IRPerfGrid` at 100³ single-voxel entities (`voxel_pool_edge = 128`),
  FULL mode, zoom 4, default lighting and shadows, under a continuous yaw
  sweep, Release build, stage profiling off: mean frame ≤ 16.67 ms and p99
  ≤ 25 ms on the M4 Max Metal reference host, recorded with
  `scripts/perf/repeat_profile.py` and committed under `docs/perf/` with
  binary, shader and runtime-script fingerprints.
- [ ] The same population reports zero overflow drops across a 15-pose yaw
  sweep, and the nine-yaw `IRCanvasStress` capture set stays RGB-identical
  across every optimization PR (`render-verify`).
- [ ] Rotation parity: the 64³ zoom-4 cell's 45°/0° frame-time ratio falls
  from 2.06× (30.3 / 14.7 ms Debug) to ≤ 1.25× at matched projected extent
  (`docs/perf/rotation-controls.md` fixture).
- [ ] Zoom parity: at fixed population the zoom 1 → zoom 4 frame-time ratio
  is ≤ 1.25× at both 0° and 45° (today 1.41× / 1.33× Debug).
- [ ] Visibility rejection is hierarchical: a region query rejects
  off-viewport populations before per-voxel compaction, the cull readback
  reports admitted-vs-total for it, and the unbounded-depth yaw suite
  (`ContinuousYawViewportHasNoDepthCutoff`,
  `StaticChunkReentersViewportAcrossFullYawTurn`) stays green.
- [ ] Simulation cadence is decoupled: the million control averages ≤ 1
  fixed update per rendered frame at 60 fps (today about 3), with a
  staggered-update or interpolation control documented under `docs/perf/`.
- [ ] The visual gates hold on `origin/master` throughout: `render-verify`
  for `shape_debug`, `canvas_stress` and `lighting`;
  `scripts/render-ao-staircase-metric.py`; the source-face geometry gates in
  `docs/design/trixel-face-reconstruction-validation.md`; and the mixed
  SDF/voxel private-canvas and SDF BOX extent gates this objective adds.
- [ ] An OpenGL host repeats the million control and the 64³ matrix with a
  `--yaw` cell axis (#3130), and the CI perf gate gates (#2817, #3471).

## Non-goals
Meshes or greedy meshing; sub-trixel geometry; a million independently
animated private-canvas objects; replacing the distance/color/id trixel
texture contract; blur, bias or inflated coverage as an edge fix; turning
shadows off as the first lever; new projection modes.

## Current state
Apple M4 Max, Metal, Debug, stage profiling on, frozen wave, zoom 4:

| Workload | Mean frame ms | Source |
|---|---:|---|
| 262,144 entities, 64³ pool, 45° | 17.4 | `docs/perf/gpu-cost-attribution.md` |
| 1,000,000 entities, 128³ pool, 0° (incomplete coverage) | 32.7 | `docs/perf/million-entity-capacity.md` |
| 1,000,000 entities, 45°, complete overflow coverage | 52.4 → 50.9 | `docs/perf/overflow-demand-capacity.md`, `gpu-cost-attribution.md` |
| 1,000,000 entities, 45°, sort network disabled (probe) | 46.5 (GPU 31.6) | `docs/perf/gpu-cost-attribution.md` |

Capacity is established (`docs/perf/million-entity-capacity.md`); the
overflow buffer now sizes from voxel demand (96 MiB at 128³); the viewport
query has no depth cutoff and static chunks reuse bounds under yaw
(`docs/perf/world-scale-visibility.md`); renderer chunks are still
allocation-slot groups, not world regions. Sorting and light propagation are
the measured GPU targets; the CPU runs about three fixed updates per
rendered frame at a million entities. Visual gates: AO contact bands are
geometric (#3484), plain detached source faces survive presentation
(#3477); open are the mixed private SDF/voxel canvas lifecycle, the SDF BOX
extent mismatch, SDF face ownership, revoxelized self-shadow patches,
OpenGL parity, plain-detached world shadows, picking on detached composites
and the 512-instance composite limit
(`docs/design/voxel-and-sdf-rendering.md` § Current acceptance work,
`docs/design/rendering-audit-todo.md`).

**Campaign:** `million-entity-render` — driven per
[`docs/agents/campaign-protocol.md`](../../agents/campaign-protocol.md);
the live worklist is
[`docs/design/campaigns/million-entity-render.md`](../campaigns/million-entity-render.md).

## Progress ledger
| Date | Epic / issue | Delta |
|---|---|---|
| 2026-09-14 | PRs #3386–#3412, #3415–#3424, #3431–#3439 | shadow reception, finite face casting, receiver alignment, rotation/subdivision audit and controls |
| 2026-09-15 | PRs #3442–#3449 | update-span coalescing, native CPU stacks, GPU-write preservation, overflow dedup, equal-depth ties |
| 2026-09-17 | PRs #3450–#3452, #3461–#3468, #3477, #3484 | light-volume pruning, unbounded yaw visibility tests, million capacity, overflow demand sizing, live-count lighting and sorting, full-frame GPU accounting, source-face presentation, geometric AO |
| 2026-09-18 | — | objective seeded from the recovered Codex session worklists |

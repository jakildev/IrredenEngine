# big wins — case studies of the largest perf improvements

One-paragraph summaries of the largest measured improvements landed.
Worth reading when sizing the budget for a new optimization — "is this
likely to be worth a session of work?"

Each entry has:

- **What** — one sentence on the change.
- **Where** — file:line or PR link.
- **Win** — the measured improvement, with the matrix cell that
  surfaced it.
- **Lesson** — what to look for next time.

Append in chronological order. New big wins (>5% mean frame ms at the
worst-case matrix cell) land here as part of the optimization PR.

---

## GPU light volume rewrite (Phase 1a)

- **What**: Replaced CPU flood-fill of the per-frame
  light-occupancy grid with a GPU dilation chain over a ping-pong
  pair of 128³ RGBA8 textures, plus per-frame seeding from a
  `LightSourceBuffer` SSBO. No more CPU populate / upload.
- **Where**: `engine/prefabs/irreden/render/systems/system_compute_light_volume.hpp`,
  shader chain `c_clear_light_volume + c_seed_light_volume +
  c_propagate_light_volume`.
- **Win**: COMPUTE_LIGHT_VOLUME CPU populate **72 ms → 0.04 ms**
  (≈1800× CPU win); total frame time dropped 8.7–10.6× at the
  262K-entity perf_grid cell. Largest single render-pipeline win
  to date.
- **Lesson**: When a per-frame CPU computation feeds a per-frame
  GPU read, **the GPU should own it**. CPU-side flood-fills are
  almost always rewritable as GPU dilation passes if the destination
  resource is a 3D texture.

## Voxel position push-at-mutation (T-289)

- **What**: Position SSBO uploads moved from per-frame `subData(0,
  liveCount × sizeof(vec4))` to mutator-time pending-range queues
  drained once per frame. Static entities cost zero bytes/frame;
  moving entities coalesce into runs.
- **Where**: `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp`,
  pending-list flush pattern documented in `cpp-ecs.md` §"No dirty
  flags on components".
- **Win**: Eliminated a steady-state CPU→GPU upload that scaled with
  pool size, freeing bandwidth on Metal where `subData` orphans the
  whole buffer.
- **Lesson**: Any per-frame "rebroadcast the CPU mirror" upload is
  suspect. Audit for push-at-mutation candidates: positions (done),
  colors (open), entity IDs (open).

## Sparse occupancy bitmask (T-287)

- **What**: Replaced the per-voxel `alpha != 0` check (read full color
  SSBO) with a `kVoxelActiveMaskBits` (32-voxel-per-word) bitfield. The
  compact shader now does a 1-bit lookup before touching the wider
  color buffer.
- **Where**: `engine/render/src/shaders/c_voxel_visibility_compact.glsl:34-36, 63-64`,
  CPU mirror in `C_VoxelPool::m_activeMask`.
- **Win**: voxelCompact pass GPU time dropped when most pool slots
  are inactive (common in editor / sparse scenes). Memory-bound win,
  not compute-bound.
- **Lesson**: For "is this slot active" predicates on large SSBOs,
  a sidecar bitfield pays off when reads dominate writes. Don't add
  the bitfield unless mutation cadence is much lower than read
  cadence (otherwise the sync cost negates the read win).

## Per-stage CPU+GPU timing infrastructure (T-275)

- **What**: Added `gpuStageTiming()` + `cpuFrameHistogram()` +
  `system_perf_stats_overlay.hpp` HUD. Not a perf win itself —
  but every subsequent optimization PR cited it for before/after
  numbers.
- **Where**: `engine/prefabs/irreden/render/gpu_stage_timing.hpp`,
  `engine/prefabs/irreden/render/systems/system_perf_stats_overlay.hpp`.
- **Win**: Indirect. Made every later optimization measurable, and
  was the precursor to PR #1016's matrix script + PR #1019's cull
  diagnostic.
- **Lesson**: Infrastructure PRs that make a class of future work
  measurable are worth landing even when they don't ship a win
  themselves. The measurement is what unlocks the optimization
  budget.

## Perf measurement scripts (PR #1016)

- **What**: Added `scripts/perf/perf_grid_matrix.sh`,
  `compare_perf_runs.py`, `perf_summary.py`. Sweeps `IRPerfGrid` /
  `IRLuaPerfGrid` across `(zoom, sub_mode, sub_base)` and produces
  markdown diffs.
- **Where**: PR #1016, `scripts/perf/`, `docs/perf/README.md`.
- **Win**: Indirect. Replaced ad-hoc "run demo, eyeball FPS overlay"
  with a reproducible diff. Every subsequent perf PR cites the
  matrix output.
- **Lesson**: Same as T-275 — measurement infrastructure pays off
  over the long tail. Especially when extracted as scripts (this
  skill never tells you how to run the matrix; it just calls the
  script).

## Voxel cull-effectiveness diagnostic (PR #1019)

- **What**: VOXEL_TO_TRIXEL_STAGE_1 reads prior frame's
  `IndirectDispatchParams.visibleCount` before zeroing — sync-free
  per-frame sample of how many voxels survived the iso-bounds cull.
  Surfaced via `getVoxelCullStats()` + the matrix scripts' new
  cull table.
- **Where**: PR #1019,
  `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp`,
  `engine/prefabs/irreden/render/gpu_stage_timing.hpp` (accumulator).
- **Win**: Indirect — but surfaced the actual root cause of the
  user-reported "zoom past full coverage and FPS still drops"
  symptom. Visible/total at zoom 8 is 0.47 when ideal is 0.02 —
  cull bounds are dominated by the shadow-feeder sweep, not the
  visible region. Issue #1020 has the proposed fix.
- **Lesson**: When a perf symptom doesn't match the obvious
  hypothesis, **measure the next layer down**. The frame time was
  growing with zoom — the next-layer-down was "is culling actually
  shrinking the working set?" and the answer was "barely."

## Pending-position-range catch-up blowup (optimize audit, 2026-05-21)

- **What**: `C_VoxelPool`'s pending-position-range queue accumulated
  one range per moving voxel set per UPDATE tick; the fixed-timestep
  loop runs several UPDATE ticks per render frame, so
  `VOXEL_TO_TRIXEL_STAGE_1`'s per-frame `std::sort` of the queue grew
  to millions of entries. Capped the queue; a saturated queue falls
  back to one whole-live-range upload.
- **Where**:
  `engine/prefabs/irreden/voxel/components/component_voxel_pool.hpp`
  (`kMaxPendingPositionRanges` + capped `queuePositionRange`),
  `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp`
  (`flushPendingPositionRanges` saturation path).
- **Win**: `IRPerfGrid` frame time fell **55–66 % on every one of the
  12 matrix cells** — zoom 8 / full 176 ms → 65 ms, zoom 1 / full
  58 ms → 23 ms. `SingleVoxelToCanvasFirst` 19–62 ms → flat ~4.9 ms.
  The feedback loop unwound: a faster render frame runs fewer
  catch-up UPDATE ticks, so the UPDATE pipeline cost dropped too.
- **Lesson**: Any UPDATE-populated / RENDER-drained queue is suspect
  when UPDATE can run multiple times per render frame. The tell is a
  system whose CPU cost tracks *frame time* rather than scene
  content — sub-tick `IR_PROFILE_SCOPE` blocks pinpoint the region.

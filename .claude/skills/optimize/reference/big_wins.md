# big wins — lessons from the largest measured improvements

Sizing guide for a new optimisation session. Append a row for any win > 5%
mean frame ms at the worst-case matrix cell, in the same PR as the fix; the
lesson is the payload, the location is where to read the pattern.

| Lesson | Where the pattern lives | Measured |
|---|---|---|
| A per-frame CPU computation that feeds a per-frame GPU read belongs on the GPU; a CPU flood-fill into a 3D texture rewrites as a GPU dilation chain | `system_compute_light_volume.hpp`; `c_clear_light_volume` + `c_seed_light_volume` + `c_propagate_light_volume` | CPU populate 72 ms → 0.04 ms; frame 8.7–10.6× faster at the 262K-entity cell |
| Any per-frame "rebroadcast the CPU mirror" upload is suspect; push at mutation time and drain pending ranges once per frame (positions done; colours and entity IDs still per-frame) | `system_voxel_to_trixel.hpp`; `.claude/rules/cpp-ecs.md` §"No dirty flags on components" | steady-state upload scaling with pool size eliminated |
| For "is this slot active" predicates over a large SSBO, a sidecar bitfield wins when reads dominate writes; skip it when mutation cadence is comparable to read cadence | `c_voxel_visibility_compact.glsl` (`kVoxelActiveMaskBits`), `C_VoxelPool::m_activeMask` | voxelCompact GPU time down on sparse scenes |
| Measurement infrastructure (per-stage CPU+GPU timing, the matrix scripts, the cull-effectiveness sample) pays for every later optimisation; land it even without a win | `gpu_stage_timing.hpp`, `system_perf_stats_overlay.hpp`, `scripts/perf/` | indirect |
| When a symptom does not match the obvious hypothesis, measure the next layer down — "is culling shrinking the working set?" (visible/total 0.47 at zoom 8 vs 0.02 ideal) | `getVoxelCullStats()`, the comparator's cull table | root cause of "zoom past full coverage and FPS still drops" |
| A queue populated per UPDATE tick and drained per RENDER frame grows with the fixed-timestep catch-up count and feeds back into frame time; cap it and fall back to one whole-range upload when saturated | `component_voxel_pool.hpp` (`kMaxPendingPositionRanges`), `system_voxel_to_trixel.hpp` (`flushPendingPositionRanges`) | `IRPerfGrid` 55–66% faster on all 12 cells (zoom 8 / full 176 → 65 ms) |

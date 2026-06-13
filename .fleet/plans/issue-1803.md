# Plan: UPDATE_VOXEL_SET_CHILDREN → PARALLEL_FOR (thread-safe pool writes)

- **Issue:** #1803 (carve-off of #1052) — **parked** until #1294 render chain lands
- **Model:** opus
- **Date:** 2026-06-13

## Scope

Make `UPDATE_VOXEL_SET_CHILDREN`'s 262K-entity tick `Concurrency::PARALLEL_FOR`
by making its shared `C_VoxelPool` writes thread-safe. One of the two dominant
UPDATE costs (#1740).

## Verified current state

- `engine/prefabs/irreden/voxel/systems/system_update_voxel_set_children.hpp` —
  SERIAL; per-entity-id tick form (`:49`). 262K `isRangeVisible`/position loop.
- Shared-state hazards in the tick:
  - **`pool.queuePositionRange(startIdx, count)`** (`component_voxel_pool.hpp:523-531`)
    → `m_pendingPositionRanges.emplace_back` — **races under concurrent ticks**
    (the real blocker).
  - `markChunkWorldBoundsDirty()` (`:373`) → idempotent bool write (benign).
  - `setEntityIdForRange(...)` (`:234-248`) → `std::fill` over **disjoint**
    per-entity voxel spans (safe by allocation invariant) + idempotent
    `m_entityIdsDirty` bool.
- No unified per-worker-merge harness exists; precedent is the `thread_local`
  buffer pattern in `system_voxel_to_trixel.hpp`.

## Approach (single path)

1. Per-worker `thread_local std::vector<std::pair<size_t,size_t>>` accumulates
   pending position ranges during the parallelFor.
2. After the implicit parallelFor barrier, a main-thread merge appends all worker
   queues into `pool.m_pendingPositionRanges` (deterministic set; order-independent
   downstream).
3. Confirm the disjoint-span invariant for `setEntityIdForRange` (each entity owns
   `[voxelStartIdx_, voxelStartIdx_+numVoxels_)`); if disjoint, the `std::fill` +
   bool writes are safe under concurrency.
4. Convert the tick to the ranged form and tag `PARALLEL_FOR` (singleton group).

Aligns with epic #226 Phase 1 (per-worker deferred state); roll the `thread_local`
pattern rather than blocking on a #226 primitive.

## Affected files

- `engine/prefabs/irreden/voxel/systems/system_update_voxel_set_children.hpp` —
  ranged-tick conversion, thread_local accumulator + merge, `kConcurrency`.
- `engine/prefabs/irreden/voxel/components/component_voxel_pool.hpp` — if a
  thread-safe `queuePositionRange` variant / merge helper is cleaner there.

## Acceptance criteria

- Output **bit-identical** (cull-on path); merged `m_pendingPositionRanges`
  contains the same set of ranges as serial (order-independent).
- Measured `update` ms reduction on IRPerfGrid `voxel_set` zoom8.
- `validateAllPipelineGroups` passes; builds clean both hosts.

## Gotchas

- Verify the disjoint-span invariant before relying on it for
  `setEntityIdForRange` — overlapping spans would make the `std::fill` a real race.
- The cull-gate (`isRangeVisible`, #1740) stays — parallelize the loop, don't drop
  the gate.
- Priority: parked behind #1294; update is secondary to render for FPS.

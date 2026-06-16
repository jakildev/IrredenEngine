# Plan: PERIODIC_IDLE_POSITION_OFFSET → PARALLEL_FOR (cheap win)

- **Issue:** #1802 (carve-off of #1052)
- **Model:** sonnet
- **Date:** 2026-06-13

## Scope

Tag `System<PERIODIC_IDLE_POSITION_OFFSET>` as `Concurrency::PARALLEL_FOR` so its
262K-entity tick fans across the IRJob pool. The cheapest piece of #1052.

## Verified current state

- `engine/prefabs/irreden/update/systems/system_periodic_idle_position_offset.hpp:24`
  — SERIAL (no `kConcurrency`); per-entity tick form.
- Its body calls `IRPrefab::Modifier::upsertBySourceInPlace(mods, ...)`
  (`engine/prefabs/irreden/common/modifier.hpp:623-638`), which mutates **only
  the passed-in entity's own `C_Modifiers::modifiersVec3_`** — no shared
  allocator, global pool, static buffer, or singleton. Verified independent per
  entity.
- In `creations/demos/perf_grid/main.cpp` it already runs in its **own singleton
  pipeline group**, satisfying the "PARALLEL_FOR must be a singleton group" rule
  (`validateAllPipelineGroups`).

## Approach (single path)

1. Add `static constexpr Concurrency kConcurrency = Concurrency::PARALLEL_FOR;`
   to the `System<PERIODIC_IDLE_POSITION_OFFSET>` specialization. Optionally
   `static constexpr int kGrainSize = 512;` (the default).
2. Build + boot `validateAllPipelineGroups` (already a singleton group → passes).
3. Confirm bit-identical output and record the `update` stage ms before/after.

No thread-safety work needed (entity-local writes). No other call sites.

## Affected files

- `engine/prefabs/irreden/update/systems/system_periodic_idle_position_offset.hpp`
  — the `kConcurrency` (+ optional `kGrainSize`) line.

## Acceptance criteria

- Builds clean (`fleet-build --target IRPerfGrid`); validator passes.
- Output **bit-identical** to SERIAL (per-entity modifier upserts are independent).
- `update` ms before/after recorded on IRPerfGrid `voxel_set` zoom8
  (`--auto-profile 240`).

## Gotchas

- The per-entity tick form populates `prepareRangedTick` (required for
  PARALLEL_FOR); only the per-archetype *batch* form is rejected — this system
  uses the per-entity form, so it's eligible.
- Priority note: update is secondary to render (voxelStage1 32ms) for FPS — this
  is a free floor-lowering win, not a frame-gate fix.

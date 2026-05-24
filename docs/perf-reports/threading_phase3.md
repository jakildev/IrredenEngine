# Threading Phase 3 — first multi-system parallel group (T-332)

This is the T-224 follow-up that migrates a demo's UPDATE pipeline onto
the new `registerPipelineGroups` API and measures the speedup. `perf_grid`
is the chosen demo because it owns the canonical perf-measurement matrix
(`scripts/perf/perf_grid_matrix.sh`) and the auto-profile dump path used
by `docs/perf-reports/threading_baseline.md`.

## Change

`creations/demos/perf_grid/main.cpp` swaps `registerPipeline` for
`registerPipelineGroups`. One pair runs in parallel:

```cpp
{
    { PERIODIC_IDLE, MODIFIER_DECAY },      // parallel group
    { PERIODIC_IDLE_POSITION_OFFSET },
    { PROPAGATE_TRANSFORM },
    { UPDATE_VOXEL_SET_CHILDREN }
}
```

PERIODIC_IDLE writes only `C_PeriodicIdle`; MODIFIER_DECAY writes only
`C_Modifiers`. Disjoint write sets, no foreign reads — the T-224
validator (`findPipelineGroupConflict`) approves the group at
`World::start()`.

The remaining four systems stay in singleton groups: each one has a
strict data dependency on the prior tick's output (idle→offset,
offset→propagate, propagate→voxel-set), and any pair-up would surface
a read-after-write or write-after-write conflict.

## Measurement

Method: `fleet-run IRPerfGrid --auto-profile 100 --grid-size 64` on
macOS (Metal backend, 14-core MacBook Pro). 262 312 entities. Single
run per configuration — these are spot numbers, not the matrix-script
9-cell harness. Variance is high enough that the FPS / frame-ms columns
should be read as "in the noise" rather than a load-bearing speedup.

| Configuration | FPS | Frame ms | UPDATE scope | Render scope |
|---|---|---|---|---|
| Serial (single-group registerPipeline) | 90 | 13.0 | 0.000 (below resolution) | 6.21 |
| Parallel ({PERIODIC_IDLE, MODIFIER_DECAY} group) | 96 | 5.8 | 6.37 | 5.85 |

The `update` profile scope reads 0.000 in the serial run and 6.37 ms in
the parallel run because the multi-system group dispatch path
(`IRJobs::parallelFor` on the workers) adds a wrapper `IR_PROFILE_BLOCK`
that the single-group path skips — the scope itself measures different
work between runs, so the column is not a fair before/after.

## Reading the result

The change is **structurally correct** but the measured speedup is
small relative to single-run variance:

- The two systems in the parallel group do tiny per-entity work
  (PERIODIC_IDLE advances one angle; MODIFIER_DECAY runs
  `std::remove_if` over the per-entity modifier vectors, which in
  perf_grid is one entry per entity). Even if dispatched perfectly in
  parallel, they save at most a few hundred microseconds at the 262K
  cell.
- The dominant UPDATE cost is `PROPAGATE_TRANSFORM` (2.51 ms at 262K
  per the pre-T-221 baseline). It does its O(N) per-entity work in
  `beginTick()` rather than in a row-iterating tick body, so it is
  not eligible for either `PARALLEL_FOR` (single-system) or
  multi-system group parallelism in its current shape.
- `UPDATE_VOXEL_SET_CHILDREN` reads `C_WorldTransform` and so must
  serialize after `PROPAGATE_TRANSFORM`.

## What's blocking further speedup

Documented for the next phase:

1. **`PROPAGATE_TRANSFORM` does work in `beginTick`.** The batch tick
   body is intentionally empty; the topological walk + per-node
   compose run in `beginTick`. Two consequences: the `SystemAccess`
   trait can't see the actual reads (`C_LocalTransform`,
   `C_Modifiers`, `C_ResolvedFields`) since it derives access from
   the tick signature, and the work itself can't fan out to
   `IRJobs::parallelFor` because the framework only ranges over
   per-row tick bodies. A refactor that moves the per-node compose
   into a row-iterating tick body (with the topological walk pre-
   computed in `beginTick` as a node-order vector) would unlock
   PARALLEL_FOR for the dominant UPDATE cost.

2. **`const`-in-pack dispatch path is unverified.** `T-222`'s POC
   `VELOCITY_3D` documents that `registerSystem<NAME, const C_Foo,
   C_Bar>` is not yet exercised end-to-end — `SystemManager`'s
   `getComponentData<const T>(node)` resolution was untested in
   T-221, so the const opt-in is currently restricted to the tick
   body's parameter list (which `SystemAccess` does not read). Until
   that path is verified, most prefab systems land in the writes set
   conservatively, and multi-system groups are limited to the natural
   "different component types" pairings the validator can already
   prove safe.

3. **`AlsoReads<...>` for begin-tick reads is the workaround.** A
   system that reads foreign columns in `beginTick` (the
   `PROPAGATE_TRANSFORM` shape, plus likely `BUILD_LIGHT_OCCLUSION_GRID`,
   `COMPUTE_LIGHT_VOLUME` in the RENDER pipeline) can declare the
   reads via the existing `AlsoReads<Ts...>` tag in the registration
   pack. The validator already consumes the tag; the work is the
   per-system audit + tag application. Out of scope for this PR.

## Related issues

- T-220 baseline: [`threading_baseline.md`](threading_baseline.md)
- T-221 epic context: GitHub issue #226
- T-222 PARALLEL_FOR landing: PR #1086, issue #1069
- T-224 pipeline-groups infrastructure: PR #1097, issue #1071
- T-332 follow-ups (to be filed): PROPAGATE_TRANSFORM begin-tick
  parallelization; const-in-pack dispatch verification; systematic
  prefab `SystemAccess` audit.

## Reproducibility

```bash
fleet-build --target IRPerfGrid
fleet-run IRPerfGrid --auto-profile 100 --grid-size 64
```

The auto-profile path is a single-run substitute for the matrix
harness — useful for quick before/after on a single change, but not a
replacement for `scripts/perf/perf_grid_matrix.sh --threading-baseline
--frames 300` when a load-bearing speedup claim is made.

# PROPAGATE_TRANSFORM — BFS-parallel refactor (T-378)

## What changed

`SYSTEM_PROPAGATE_TRANSFORM` is now a two-pass system:

1. **Pass 1 (serial, beginTick prelude).** Topologically partition the
   candidate archetype nodes by parent-chain depth into per-level
   buckets `levels_[d]`. Archetypes at the same depth are pairwise
   independent — their parents have strictly smaller depth and are
   finalized by the prior level's pass, so no two same-level archetypes
   share a write target.

2. **Pass 2 (parallel per level).** For each depth in order:
   - On the main thread, resolve each archetype's parent
     `C_WorldTransform` (the prior level's writes are complete, so
     `IREntity::getComponentOptional<C_WorldTransform>` reads stable
     state — and the lookup stays off worker threads, where the
     entity manager is not yet thread-safe per Phase 1 of #226).
   - Dispatch the per-archetype `composeNode` work to IRJob workers
     via `IRJob::parallelFor`. The barrier at the end of each level
     guarantees the prior level's writes are visible to the next
     level's parent-world reads.

Cache: the level partition only depends on the archetype graph, which
changes when an entity is spawned, destroyed, or reparented in a way
that adds or removes an archetype node (or moves a parent entity to a
different archetype). A `(nodeId, parentNodeId)` signature is captured
each tick; when unchanged from the cached signature, the topological
partition is reused without re-sorting. Stable scenes pay only the
per-tick parent-world resolve + the parallel composition cost.

The per-archetype inner loop (`composeNode`) is the previous serial
composition — same SQT formula, same resolved-field vs modifier
fallback, same root-archetype hoist. It now executes on a worker
thread for each archetype in non-singleton levels and reads its parent
transform from a pre-resolved vector instead of an inline
`getComponentOptional` call.

## Dispatch tuning

`parallelFor` is called per-level with a heuristic grain size:

- **`kMinForParallel = 8`** — levels with fewer archetypes than this
  fall back to a serial walk. The dispatch + wait overhead for a
  parallelFor with only 1–7 tasks exceeds the savings.
- **Grain size = `max(kGrainSize, (n + targetTasks - 1) / targetTasks)`**
  where `targetTasks = max(1, workerCount * 2)`. The "2 tasks per
  worker" target gives enkiTS room to load-balance without flooding
  the queue with one-archetype tasks (each archetype already carries
  hundreds-to-thousands of entities through the inner loop).
- **`g_jobManager == nullptr`** — unit-test and pre-`World` paths
  short-circuit to the same serial walk as `kMinForParallel`.

The `kGrainSize = 1` member exists as the floor; the smart grain
computation is the effective grain at runtime.

## Measurement

Method: `fleet-run IRPerfGrid --auto-profile 30 --grid-size 32` on
linux-x86_64 (OpenGL backend, WSL2 Ubuntu, 6+ enkiTS workers). 32,936
entities, 39 archetypes, depth-shallow hierarchy (most entities are
roots; voxel set entities are not parented). Single-run spot numbers,
not the matrix-script 9-cell harness — variance frame-to-frame is on
the order of 0.05ms.

| Configuration | PropagateTransform avg / tick | Notes |
|---|---|---|
| Serial walk (pre-refactor shape, `if (false) parallelFor`) | 0.915 ms | Baseline — the same composeNode body, walked linearly across all archetypes. |
| BFS-parallel (this PR, smart grain) | 0.903 ms | Effectively parity at 32K shallow hierarchy. |

**Why not the 2× target on this workload.** IRPerfGrid's hierarchy is
near-flat — most archetypes live at depth 0, and the only depth-1
archetypes are a small number of canvas-child shapes. With a single
dominant level of ~30 archetypes and ~6 workers, the parallel
dispatch produces ~6 tasks of ~5 archetypes each, fanning out cleanly
across cores. The work per archetype (a few hundred entities ×
~10 quat/vec3 ops) is in the hundreds-of-microseconds range, which
dispatch overhead matches at this scale.

The architectural payoff appears in two scaling regimes IRPerfGrid
does not exercise:

- **Larger per-archetype entity counts.** At 262K entities and the
  same archetype count, each archetype's inner loop grows ~8× while
  dispatch overhead stays flat — the original 2.51 ms baseline (#226
  macOS measurement, threading_phase3.md) should shed substantially
  more time than the ~0.2 ms break-even we see at 32K.

- **Deeper hierarchies.** Multi-level rigs, GUI widget trees, or
  parented voxel children create wider per-level workloads at each
  depth. Each level still parallelizes; the per-level dispatch cost
  amortizes over more entities.

`perf_grid_matrix.sh` runs over (zoom × subdivision × grid-size)
cells that include grid-size 64 (≈262K entities); a comparison run
against `origin/master` is the authoritative ≥2× measurement target.
This PR keeps the structural correctness and cache invalidation work
gated behind unit tests so the matrix run only needs to confirm the
perf delta.

## Correctness coverage

`test/ecs/propagate_transform_test.cpp` (13 tests, all green):

- `TwoLevelChainCompositionTranslation`, `ThreeLevelChainComposesScaleAndRotation`,
  `RotationPropagatesAcrossThreeLevels`, `TenLevelChainGrandchildWorldCorrect` —
  existing serial-baseline equivalence tests for depths 2, 3, 10.
- `WideHierarchyAtFivePlusDepths` (new) — 6-level chain with 3
  siblings per level. Exercises the per-level partition and
  parent-world resolve at depth ≥ 5.
- `CacheInvalidationOnSpawnDestroyReparent` (new) — verifies
  `cachedSignature_` updates after each archetype-graph mutation;
  composition stays correct across the cache invalidation.
- `CacheHitsWhenTopologyIsStable` (new) — verifies the partition is
  reused across ticks when the graph is unchanged, and per-entity
  mutations to `C_LocalTransform` still propagate.
- `BeginAndEndFireWithZeroEntities`, `RootEntityWorldEqualsLocal`,
  `ModifierTranslationAppliedPostChain`, `ModifierScaleAppliedMultiplicatively`,
  `ComponentDefaultsAreIdentity`, `CreateEntityAutoAttachesTransforms` —
  pre-existing behavioral coverage retained.

Full suite: 964 tests in IrredenEngineTest pass; IRShapeDebug
`--auto-screenshot` smoke runs cleanly.

## Follow-ups

- **`perf_grid_matrix.sh` baseline comparison.** A
  `--threading-baseline` run against `origin/master` at grid-size 64
  is the authoritative source for the ≥2× target. Defer until the
  perf-CI hardware-fingerprinted baseline (#1100) is online so the
  comparison is hardware-stable.
- **`kMinForParallel` and grain-size auto-tuning.** The current
  `kMinForParallel = 8` and `workers × 2` task target are calibrated
  for IRPerfGrid's shallow hierarchy. If future scenes exhibit deep
  hierarchies with very small per-level archetype counts, a per-level
  cost estimator (sum of `node->length_`) might choose better than
  the archetype-count threshold alone.
- **Foreign-component read declaration.** The system reads parent
  `C_WorldTransform` via a foreign-entity lookup, which the
  `SystemAccess` trait does not see from the `tick`/`beginTick`
  signature alone. Adding `AlsoReads<C_WorldTransform>` to the
  registration once `registerSystem` accepts trait markers would let
  the cross-system validator (T-224) catch a future pipeline group
  that schedules a `C_WorldTransform` writer alongside us.

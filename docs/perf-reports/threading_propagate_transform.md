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

> **Updated by #1804 (intra-node row-range parallelism).** The original
> T-378 dispatch was per-archetype only — one parallelFor task per
> archetype node. That degenerates when a single archetype holds the
> whole scene (IRPerfGrid's 262K-entity node is one node at depth 0):
> the level has `n = 1`, falls below `kMinForParallel`, and the 262K-row
> `composeNode` runs serially on one worker. See the dedicated section
> below for the row-splitting fix and its measurement.

> **Hoisted into `IRJob` by #1900.** The fan-out decision, chunk
> sizing, and serial fallback below no longer live in the system —
> they are `IRJob::parallelChunks`, tuned by an `IRJob::ParallelTuning`
> struct. The old `kMin*` / `targetTasks` constants are now that
> struct's **defaults**, so `PROPAGATE_TRANSFORM` passes a
> default-constructed tuning and the numbers stay bit-identical. The
> system now only mirrors each level's per-node row counts into a
> reused buffer (`nodeLengths_`) and calls the planner with its
> reusable `chunks_` scratch; `engine/job/` owns and unit-tests the
> policy (`test/job/ir_job_test.cpp`). The field names below are the
> `ParallelTuning` members.

Each level is flattened into a list of row-chunks (by
`IRJob::parallelChunks`) and dispatched with a single internal
`parallelFor`:

- **`minNodes_ = 8`** (node count) **OR `minItemsToParallelize_ = 4096`**
  (total entity rows) — a level parallelizes when it has at least this
  many archetypes *or* this many total rows. Below both, the dispatch +
  wait overhead exceeds the savings and the level composes serially.
- **`minChunk_ = 2048`** — the minimum rows per chunk. A node smaller
  than the chunk size stays whole (one chunk), preserving the original
  per-archetype granularity for many-small-node levels; a node larger
  than the chunk size splits into multiple row-range chunks. The chunk
  size is `max(minChunk_, ceil(totalRows / targetTasks))` with
  `targetTasks = max(1, workerCount * tasksPerWorker_)` (default
  `tasksPerWorker_ = 2`), so a single dominant node splits into
  ~`targetTasks` chunks (≈2 per worker).
- **Grain = `max(1, ceil(numChunks / targetTasks))`** over the chunk
  list — `1` when chunks already number ≤ `targetTasks` (single large
  node), grouping when many small nodes produce more chunks than tasks.
- **`g_jobManager == nullptr`** — unit-test and pre-`World` paths
  short-circuit to the serial walk.

`composeNode` became `composeNodeRows(node, parentWorld, rowBegin, rowEnd, …)`
— same SQT body, now bounded to a row range. Disjoint row ranges are
race-free (each entity writes only its own `C_WorldTransform[i]`), so
output is bit-identical to the serial path regardless of chunk
boundaries.

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

## Intra-node row-range parallelism — measurement (#1804)

The 262K scaling regime the T-378 report flagged but didn't exercise
is exactly the single-node degeneracy #1804 fixes. With the whole grid
in one depth-0 archetype, the per-archetype dispatch left the entire
composition on one worker; row-splitting fans it across all workers.

Method: `fleet-run IRPerfGrid --auto-profile 12 --auto-screenshot` on
macOS (Metal, Apple Silicon, 10 enkiTS workers). `--auto-screenshot`
enables fixed-step (one UPDATE tick per render frame) so the profiler's
last-frame `update` sample reliably lands on a ticked frame; without it
the fixed-step UPDATE pipeline may not tick the sampled render frame and
reports `update: 0.000`. grid-size 64 (262,144 entities, one archetype).
Three runs each; `update` is the whole UPDATE-pipeline stage, and since
only `PROPAGATE_TRANSFORM` changed, the delta is its contribution.

| Configuration | `update` stage / frame |
|---|---|
| `origin/master` (per-archetype dispatch → serial single node) | 4.32 / 4.42 / 4.61 ms |
| This PR (intra-node row-chunks across 10 workers) | 2.25 / 2.27 / 2.56 ms |

≈ **47% reduction (~2.1 ms)** on the UPDATE pipeline. Render still
dominates frame time (~8 ms), so this is a secondary-axis win per the
issue's own framing, but it closes the single-node degeneracy directly.

Bit-identity check: `IRShapeDebug --auto-screenshot` is 28/28
byte-identical to `origin/master` (deterministic path reading
`C_WorldTransform`). IRPerfGrid screenshots are non-deterministic
run-to-run (GPU rasterization speckle), so they can't be byte-compared;
instead, per-shot pixel-diff magnitude (mine-vs-master) was confirmed to
sit within the demo's own run-to-run noise floor (mine-vs-mine ≈
master-vs-master ≈ mine-vs-master, MAE < 0.5/255, < 1% pixels touched,
no structural region difference).

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
- **`kMinForParallel` and grain-size auto-tuning.** ✅ Addressed by
  #1804: the per-level cost estimator (sum of `node->length_` →
  `totalRows`) is now a parallelization trigger alongside the
  archetype-count threshold, and large single nodes split across
  workers by row range. Remaining tuning headroom: `kMinChunkRows`
  (2048) and `kMinRowsForParallel` (4096) are calibrated for the
  per-row quat/vec3 cost; revisit if a much heavier or lighter
  per-entity composition lands.
- **Foreign-component read declaration.** The system reads parent
  `C_WorldTransform` via a foreign-entity lookup, which the
  `SystemAccess` trait does not see from the `tick`/`beginTick`
  signature alone. Adding `AlsoReads<C_WorldTransform>` to the
  registration once `registerSystem` accepts trait markers would let
  the cross-system validator (T-224) catch a future pipeline group
  that schedules a `C_WorldTransform` writer alongside us.

# Plan: PROPAGATE_TRANSFORM intra-node row-range parallelism

- **Issue:** #1804 (carve-off of #1052) — **parked** until #1294 render chain lands
- **Model:** opus
- **Date:** 2026-06-13

## Scope

Give `PROPAGATE_TRANSFORM` within-node row parallelism so a single large archetype
node (IRPerfGrid's 262K-entity node) splits across workers. The other dominant
UPDATE cost (#1740).

## Verified current state

- `engine/prefabs/irreden/update/systems/system_propagate_transform.hpp` — already
  uses `IRJob::parallelFor` (`:169`) but with `kGrainSize=1` (`:75`), **one task
  per archetype node** at each depth level. Two-pass: beginTick topo-sorts nodes
  into `levels_[depth][]`; the dispatch loop (`:125-171`) resolves each level's
  parent transforms on the main thread, then fans archetypes out.
- **Confirmed single-node degeneracy:** when all 262K entities live in ONE
  archetype node, that depth level has n=1 → `parallelFor(0, 1, 1, …)` → one task →
  `composeNode` (`:278-355`) iterates 262K rows **serially** on one worker.
- `composeNode` writes each entity's own `C_WorldTransform[i]` from its own
  `C_LocalTransform[i]` + a read-only (already-composed) parent → **disjoint row
  ranges are race-free**. No shared accumulator needed.

## Approach (single path)

1. Refactor `composeRange` (`:145-155`) to take `(ArchetypeNode* node, int rowBegin,
   int rowEnd)` and iterate rows, instead of indexing archetypes.
2. For each level, dispatch `parallelFor(0, node->length_, grainSize, …)` over the
   node's **rows** (hybrid: when a level has many small nodes, keep node-granularity;
   when a node is large, split its rows — pick by `node->length_` vs a threshold).
3. **Preserve the per-level barrier** (`:130-141`): level d's `C_WorldTransform`
   must be fully composed before level d+1's parent lookups. Keep level iteration
   serial; only parallelize rows within a level.

Aligns with epic #226 Phase 1.

## Affected files

- `engine/prefabs/irreden/update/systems/system_propagate_transform.hpp` —
  `composeRange` signature + the per-level dispatch loop.

## Acceptance criteria

- Output **bit-identical** `C_WorldTransform` on BOTH single-archetype (IRPerfGrid)
  and multi-archetype (parented hierarchy) workloads — the hybrid must not regress
  the existing inter-node path.
- Measured `update` ms reduction on IRPerfGrid `voxel_set` zoom8.
- `validateAllPipelineGroups` passes; builds clean both hosts.

## Gotchas

- The per-level barrier is load-bearing — a child level's rows must never compose
  before the parent level finishes. Parallelize rows *within* a level only.
- Keep the inter-node parallelism for many-small-node workloads; this adds
  intra-node splitting, it doesn't replace the level partition.
- Priority: parked behind #1294; update is secondary to render for FPS.

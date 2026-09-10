# engine/prefabs/irreden/spatial/ — world-space neighbour + placement queries

Two separate surfaces live here, answering two different questions:

| Question | Surface | Contract |
|---|---|---|
| *Which **entities** are near P?* | `SpatialGrid` / `IRSpatial.queryRadius` | [`docs/design/lua-world-space-neighbour-query.md`](../../../../docs/design/lua-world-space-neighbour-query.md) |
| *Where is valid **space** near P?* | the chunked-field placement kit | [`docs/design/chunked-field-placement-kit.md`](../../../../docs/design/chunked-field-placement-kit.md) |

They compose and neither subsumes the other. The rest of this file covers the
entity index; the placement kit has its own section at the bottom.

## The entity index

A world-3D spatial index so gameplay systems (Lua or C++) can find *nearby*
entities — collision, neighbour avoidance, proximity triggers — without an
O(N²) all-pairs scan and without the per-neighbour foreign-read footgun.

The locked design + invariant is
[`docs/design/lua-world-space-neighbour-query.md`](../../../../docs/design/lua-world-space-neighbour-query.md).
Read it first.

## The invariant

> A consumer obtains nearby entities from a **world-space spatial index that
> is rebuilt once per frame and queried as a batch**. The query returns a
> contiguous vector of `{EntityId, position}` records; the consumer iterates
> that vector. It **never** resolves neighbours by calling a per-entity
> foreign accessor (`getComponent<T>` in C++, `IREntity.getLuaField` in Lua)
> once per candidate inside its tick.

Returning the position **inline** on every hit is what makes the footgun
unreachable: the caller already has each neighbour's position, so it never
needs a foreign `C_WorldTransform` read. This is the spatial-query corollary
of the batched-foreign-entity rule in
[`.claude/rules/cpp-ecs.md`](../../../../.claude/rules/cpp-ecs.md)
§"Foreign-entity lookups".

## Files

| File | What |
|------|------|
| `spatial_grid.hpp` | `IRPrefab::Spatial::SpatialGrid` — world-3D uniform cell hash + `SpatialHit{id_, pos_}`. Header-only data structure; allocation-disciplined (Pattern B). |
| `components/component_spatial_index.hpp` | `C_SpatialIndex` (singleton owning the grid) + `C_SpatialQueryable` (opt-in tag). |
| `systems/system_build_spatial_index.hpp` | `BUILD_SPATIAL_INDEX` — rebuilds the singleton each frame. |
| `spatial_query.hpp` | `IRPrefab::Spatial::queryRadius` / `queryAabb` free functions over the singleton (C++ surface). |

The Lua surface (`IRSpatial.queryRadius`) lives with the other Lua bindings
in `engine/script/include/irreden/script/lua_spatial_bindings.hpp`, wired
into `bindLuaDrivenEcs()`.

## Pipeline wiring (a creation opts in)

`BUILD_SPATIAL_INDEX` is **not** in any default pipeline — a creation that
wants neighbour queries registers it explicitly:

```cpp
IRSystem::registerPipeline(IRTime::Events::UPDATE, {
    IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),   // world positions current
    IRSystem::createSystem<IRSystem::BUILD_SPATIAL_INDEX>(),   // then rebuild the index
    myNeighbourConsumerSystem,                                 // then query it
});
```

Two ordering rules:

- **After `PROPAGATE_TRANSFORM`** so `C_WorldTransform.translation_` is the
  final world position before it's indexed.
- **Before any consumer** that calls `IRPrefab::Spatial::queryRadius` (C++
  system or Lua tick).

The system writes the `C_SpatialIndex` singleton (not an archetype column the
`SystemAccess` validator can see), so keep it in its **own pipeline group** —
don't co-execute it with anything else that touches the index.

## Allocation discipline

`spatial_grid.hpp` is **Pattern B**: `queryRadius` / `queryAabb` write into a
caller-owned `std::vector<SpatialHit>` (clear-then-fill) and never allocate a
temporary set/vector per query — the contrast is `render/iso_spatial_hash.hpp`,
the iso-screen render-cull index, which allocates an `unordered_set` + vector
on every query. Build reuses bucket capacity across frames: `clear()` empties
only the cells touched last frame and never frees their vectors or erases map
nodes, so after a short warm-up the build + query hot paths perform zero heap
allocation. Pass a reused scratch vector to every query to keep the caller
side allocation-free too.

## Opt-in tagging

Only entities carrying **both** `C_WorldTransform` and `C_SpatialQueryable`
are indexed. The tag is explicit so a world canvas's static voxels — and any
other transform-bearing entity nobody queries for — pay nothing.

---

## The chunked-field placement kit

**Locked contract:** [`docs/design/chunked-field-placement-kit.md`](../../../../docs/design/chunked-field-placement-kit.md).
Read it before touching any `chunked_field` / `field_*` header — every design
decision is pinned there as `D1`–`D10`, and code that disagrees with the doc is
the bug.

The invariant, in one sentence:

> A consumer that needs valid *positions* — "K cells with clearance ≥ c, spacing
> ≥ s, in the anchor's region, near the anchor" — gets them from a chunked field
> whose per-chunk summaries prune before any cell is touched. It never
> materializes a whole-grid obstacle table, never runs an O(r²) clearance kernel
> per cell at query time, and never sorts every candidate in the world.

| File | What |
|------|------|
| `chunked_field.hpp` | `ChunkedField2D<T>` — sparse map of dense 32×32 cell chunks, per-chunk `min_`/`max_`/`nonZeroCount_`/`dirty_` summaries, `FieldChunkKey` (2× int32) |
| `field_clearance.hpp` | capped integer **squared** EDT (Felzenszwalb–Huttenlocher), incremental over the dirty set |
| `field_regions.hpp` | per-chunk connected components + seam-stitching union-find |
| `field_placement.hpp` | `PlacementField`, seeded Poisson-disk draw, `queryPlacements` + `PlacementQueryStats` |

Which of those exist yet is tracked in **one** place — the doc's "Migration
status" table. Don't mirror per-file status here; two owners of the same
status is how a file table starts lying.

Four things to know before editing any of them:

- **Not a system.** No `SystemName` entry, no component, no pipeline wiring —
  plain types and free functions, like `IRMath::SDF::evaluateGrid`. A field is
  caller-mutated, so unlike `BUILD_SPATIAL_INDEX` it has nothing to rebuild from
  each frame. A creation embeds the kit in its own bake system.
- **A "field chunk" is not a residency chunk.** Field chunks tile 2D **cell**
  space; `IRConstants::kChunkSize` chunks tile 3D **voxel** space; and
  `IRRender::kVoxelChunkSize` is a GPU bucket that is not spatial at all. They
  share the number 32 for cognitive alignment and nothing else. Say "field
  chunk" in code and comments, never bare "chunk".
- **Everything is integer, inside a bounded domain.** Clearance is *squared*
  cell distance capped at `maxClearance²`; the Poisson draw rejection-samples an
  integer annulus. No `sqrt`, no libm transcendental, no
  `std::uniform_*_distribution` (not portable across standard libraries). That
  discipline is **necessary and not sufficient** for byte-identical results
  under a fixed seed — it fixes the word *stream*, not which word goes *where*,
  and it says nothing about `unordered_map` iteration order. The draw's word
  consumption and every canonical traversal order are locked in doc §D7; treat
  a change to either as a contract change, not an implementation detail.
  `maxClearance`, `minSpacing` and the query radius `c` are bounded by
  `kMaxClearanceCells = 1024`, because int32 `n²` overflows at `n = 46,341`; and
  every EDT intermediate is `int64` regardless, since the F–H term `f[q] + q²`
  is bounded by the *window row length*, not by the cap (doc §D4).
- **Cell → field chunk is floor division, not truncating division.**
  `cell >> 5` / `cell & 31` — which in C++23 is exactly floor-divide with a
  non-negative remainder, so cell `(-1, -1)` is chunk `(-1, -1)` local
  `(31, 31)`. `cell / 32` would say chunk `0` local `-1` and index a dense array
  out of bounds. Same rule the voxel-side residency helper documents at
  `engine/prefabs/irreden/world/chunk_coord.hpp:38` (doc §D2).
- **Presence is map membership, not allocation.** `clear()` makes a field chunk
  logically *absent* (D4 then reads its cells as **occupied**) while its dense
  buffer goes to a free list for reuse — that is how Pattern B and D4 coexist.
  A present all-zero chunk means "all free" and is the *opposite* state; never
  evict one because `nonZeroCount_ == 0` (doc §D2).
- **Region labels are epoch-scoped.** Global ids are *not* stable across
  `update()`. Caching a label across an update and comparing it later is a bug.

Allocation discipline is the same *allocation* Pattern B as `spatial_grid.hpp`
(see above): chunks retain capacity, queries fill a caller-owned out-vector.

Tests live in `test/ecs/`, beside `spatial_grid_test.cpp`, and each new `.cpp`
must be added to the explicit `add_executable(IrredenEngineTest …)` list in
`test/CMakeLists.txt` — it is not a glob, and an unlisted file silently never
builds.

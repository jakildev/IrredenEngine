# engine/prefabs/irreden/spatial/ — world-space neighbour queries

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

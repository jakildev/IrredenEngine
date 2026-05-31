# World-space neighbour / spatial-query surface for Lua (and C++) systems

- **Status:** proposed (design doc lands first; implementation tracked by a
  separate `**Blocked by:**` task — see "Migration status" below).
- **Supersedes the premise of:** #1354 (which assumed there is *no* Lua
  foreign-entity read surface — there is; see "What already ships").
- **Owning subsystem:** `engine/prefabs/irreden/` (a prefab system + singleton
  component), **not** engine core.

---

## The invariant this establishes

> A gameplay system that needs *nearby* entities — collision resolution,
> neighbour avoidance / boids, flow-fields, proximity triggers — obtains them
> from a **world-space spatial index that is rebuilt once per frame and queried
> as a batch**. The query returns a contiguous vector of `{EntityId, position}`
> records; the consumer iterates that vector. A consumer **never** resolves
> neighbours by calling a per-entity foreign accessor (`getComponent<T>` in
> C++, `IREntity.getLuaField` in Lua) once per candidate inside its tick.

This is the spatial-query corollary of the existing batched-foreign-entity rule
in [`.claude/rules/cpp-ecs.md`](../../.claude/rules/cpp-ecs.md) §"Foreign-entity
lookups": *"batch the foreign entities as a vector … the consumer iterates the
vector, not individual events."* A per-neighbour `getLuaField` loop is exactly
the hash-map-lookup-per-pair footgun that rule forbids — so a neighbour surface
that hands back ids without their data, forcing the caller to loop foreign
reads, would ship the anti-pattern. The query batches the data the caller needs
**by construction**, so the footgun is unreachable from Lua.

---

## What already ships (and what does not)

The foreign-entity *read* surface #1354 asked for already exists, so the design
here is **only** the neighbour-find half:

- `IREntity.getLuaField(entity, componentDef, fieldIndex)` — zero-string
  hot-path read of one field of a foreign entity's component
  (`engine/script/src/lua_script.cpp:561-576`, since #519).
- `IREntity.getLuaComponent` / `setLuaField` / `hasLuaComponent` /
  `singleton` round out the surface (`lua_script.cpp:490-597`).
- Inside a tick, the iterating row's `EntityId` is reachable via
  `arch.entityAt(i)` (`lua_script.cpp:885-887`), so a tick already has both a
  foreign id and a read primitive.

Two gaps remain, and they are the reason this subsystem exists:

1. **No way to *find* the nearby entities.** There is no world-space
   `query(center, radius) -> {EntityId…}` anywhere. The only spatial index in
   tree, `engine/prefabs/irreden/render/iso_spatial_hash.hpp`, operates in
   **iso-screen 2D** (used only by the cull-minimap debug overlay) and
   allocates a set + vector per query — it is a render-culling tool, not a
   reusable world-space neighbour index. Collision today is genuine O(N²)
   all-pairs (`system_collision_note_platform.hpp:107-190`). World-voxel
   occupancy/light grids (#360) store *voxel bits*, not entity ids.

2. **`getLuaField` reads Lua-defined components only.** It does an **unchecked**
   `static_cast<IComponentDataLuaTyped*>` (`lua_script.cpp:568`), so reading a
   hand-written C++ component (e.g. `C_WorldTransform`, where *position* lives)
   through it is undefined behaviour, not a clean nil. A neighbour's position
   is therefore **not** reachable from Lua via `getLuaField`. This is why the
   query must return positions **inline** (see decision D1) rather than ids
   alone.

---

## Proposed shape

A prefab subsystem mirroring the established singleton-state + rebuild-system
pattern (`C_ActiveLodLevel` ← `LOD_UPDATE`; `C_GlobalModifiers`):

- **`spatial_grid.hpp`** (header-only, `engine/prefabs/irreden/spatial/`): a
  world-3D uniform cell hash. Insert by `vec3` / AABB; `queryRadius(center,
  radius, out)` and `queryAabb(min, max, out)` write into a caller-owned
  `std::vector<…>` (Pattern B — no per-query allocation; reuse scratch buffers
  across frames). Structurally like `IsoSpatialHash` but world-space and
  allocation-disciplined.
- **`C_SpatialIndex`** — singleton component owning the grid + its reserved
  capacity buffers (reused, not reallocated, each frame).
- **`BUILD_SPATIAL_INDEX`** — a system in the UPDATE pipeline that iterates
  `C_WorldTransform` entities carrying an opt-in `C_SpatialQueryable` tag
  (so the world canvas's static voxels are *not* indexed), clears the grid in
  `beginTick`, inserts `(EntityId, translation_)` per row, and runs **before**
  any consumer. Add its `SystemName` enum entry first
  (`engine/system/include/irreden/system/ir_system_types.hpp`) per
  `engine/CLAUDE.md`.
- **Query surface (C++)** — `IRPrefab::Spatial::queryRadius(vec3 center, float
  radius, std::vector<SpatialHit>& out)` where `SpatialHit { EntityId id; vec3
  pos; }`. Out-param, caller owns capacity.
- **Query surface (Lua)** — a binding next to `bindLuaDrivenEcs` that returns a
  Lua array of `{id, x, y, z}` records. Returning the position **inline** is
  what keeps the Lua path footgun-safe *and* sidesteps the
  C++-component-foreign-read gap (gap 2 above): the caller never needs a
  foreign `C_WorldTransform` read.

---

## Open design decisions (to finalize in the implementation plan)

- **D1 — query payload.** Return `{id, pos}` **inline** (recommended: caller is
  self-sufficient, no foreign C++-component read needed, footgun unreachable),
  vs ids-only (cheaper to build but then requires extending the Lua foreign-read
  path with a guarded C++-component branch — strictly more work and re-opens
  the unchecked-`static_cast` surface). Lean: inline.
- **D2 — exact home.** `engine/prefabs/irreden/spatial/` (new domain) vs folding
  into an existing `update/` grouping. Lean: new `spatial/` domain.
- **D3 — index shape.** Uniform cell hash (simple, good for roughly-uniform
  density) vs something hierarchical. Lean: uniform hash sized to the dominant
  query radius; revisit only if profiling shows clustering pathologies.

---

## What consumes this

- Lua neighbour / collision / boids / flow-field systems (the #1354 motivation).
- A future C++ collision broadphase could replace the O(N²) all-pairs scan in
  `system_collision_note_platform.hpp` with a `queryAabb` per collider — out of
  scope here, but the surface is designed to serve it.

## Migration status

- This doc lands **first** (docs-only PR) so the model is reviewable
  independently of the implementation.
- Implementation is a **separate task** carrying `**Blocked by:** #<this PR>`;
  it becomes claimable when this doc merges. PR #1366 (the #1354 investigation
  PR) is investigation-only (no code) and is superseded by that task.

## What to verify

- `BUILD_SPATIAL_INDEX` runs before every consumer (pipeline ordering).
- Zero per-frame / per-query heap allocation in the build + query hot paths
  (`optimize` pass; the `IsoSpatialHash` per-query `unordered_set`+vector is the
  anti-example).
- A Lua collision/neighbour demo resolves contacts via one batched query, with
  **no** per-neighbour `getLuaField` loop in the tick.
- Stale-doc cleanup folded into the implementation: the `iso_spatial_hash.hpp`
  "not referenced by any system" header comment is wrong (it *is* used by the
  cull-minimap), and `engine/prefabs/irreden/update/CLAUDE.md` references a
  non-existent `nav_query.hpp`.

## References

- Foreign-read surface: `engine/script/src/lua_script.cpp:490-597,885-887`;
  `engine/script/CLAUDE.md` §"Two-tier accessor contract".
- Batched-foreign-entity rule: `.claude/rules/cpp-ecs.md` §"Foreign-entity
  lookups"; producer/consumer exemplar `system_collision_note_platform.hpp`
  (batched-archetype-query) → `C_ContactEvent` consumers.
- Existing iso-screen index (not reusable here):
  `engine/prefabs/irreden/render/iso_spatial_hash.hpp`.
- Singleton-state + rebuild-system precedent: `LOD_UPDATE` / `C_ActiveLodLevel`.

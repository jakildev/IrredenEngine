# engine/world/ — the runtime root

The `World` class is the singleton that owns every manager, brings the loop
up, and tears it down. There is exactly one `World` per process, constructed
by `IREngine::init()` and destroyed at shutdown.

## Entry point

`engine/world/include/irreden/world.hpp` — declares `class World`.

## Chunk residency (Epic E)

`engine/world/include/irreden/world/chunk_residency.hpp` declares
`IRWorld::ChunkResidencyManager` — the resident-set + per-chunk voxel
sub-pool + entity manifest. **Not** owned by `World` — creations that
opt into streaming construct one explicitly. Single-chunk creations
ignore it entirely (zero-overhead). Companion chunk-coord utilities
live in [`engine/prefabs/irreden/world/`](../prefabs/irreden/world/);
full design contract in
[`docs/design/world-streaming.md`](../../docs/design/world-streaming.md).

### Camera-aware prefetch (E3)

`beginFrame(vec3 cameraWorldVoxel)` drives both the E2 eviction policy
(Euclidean distance + hysteresis → EVICTING) and the E3 chunk-coordinate
derivation. `tickPrefetch()` then scans a Chebyshev ring of
`Config::prefetchRadiusChunks_` around the derived chunk coordinate and
`requestResident`s every chunk in the ring; eviction is left entirely to
`beginFrame` + `endFrame` (no per-ring eviction in `tickPrefetch`). The
distance from camera to each slot's chunk center is written to
`ChunkResidencySlot::distanceVoxels_` for the budget-gate and future sorting.

### Upload-bandwidth cap + low-LOD billboard (T-358)

Opt in via `Config::deferredUpload_ = true`. With the toggle on,
`requestResident` enqueues the chunk in `LOADING` instead of
synchronously transitioning to `RESIDENT`, and `flushUploads(maxBytes)`
drains the queue each frame in (priority, distance) order capped at
the byte budget. `FORCED` requests bypass the budget. A
single-chunk-exceeds-budget guard always drains at least one
non-forced entry per call so streaming can never stall on a chunk
larger than the cap. The default budget lives in
`Config::defaultUploadBudgetBytes_` (4 MiB, matching the design doc's
≈240 MiB/s @ 60 fps target); pass `0` to `flushUploads` to use it.

Each `ChunkResidencySlot` carries low-LOD AABB billboard metadata —
`aabbColor_` (default grey), `aabbMinVoxel_` / `aabbMaxVoxel_` (full
chunk by default), `lowLodFlags_`. The renderer's low-LOD pass
iterates `forEachLowLodSlot` to spawn a `BOX` `C_ShapeDescriptor` per
non-`RESIDENT` chunk; once the chunk reaches `RESIDENT` the voxel pool
takes over and the billboard is dropped. Full design and the on-disk
`BBOX` chunk record that will eventually replace the defaults are in
[`docs/design/world-streaming.md`](../../docs/design/world-streaming.md)
§"Topic 4 — Upload-bandwidth cap + low-LOD fallback".

When `deferredUpload_` is false (the default), the legacy E1
synchronous behavior is preserved: `requestResident` reaches
`RESIDENT` inline, `flushUploads` is a no-op, and the low-LOD fields
remain at their defaults but no chunk is ever in the non-`RESIDENT`
state long enough for them to matter. Existing E1+E2+E6 consumers
keep working unchanged.

`engine/world/include/irreden/world/chunk_persistence.hpp` declares
`IRWorld::ChunkVoxelDiskPersistence` — per-chunk `.vxs` save/load under a
`<saveRoot>/chunks/<x_div_64>/<y_div_64>/` two-level directory tree. One
file per chunk; filename embeds the signed chunk coord (e.g.
`chunks/0/-1/+00003_-00007_+00011.vxs`). When wired
on `ChunkResidencyManager::Config::persistence_`, the manager loads
the chunk slice from disk on first `requestResident` and saves dirty
chunks on `requestEvict`. `flushPendingSaves()` is the editor's
save-all hook. Synchronous in v1; E3 lifts the same calls into an
async worker pool without changing the surface. Entity-level state
(components beyond the chunk's voxel pool) belongs to the parallel
world-snapshot path (#199), not this layer.

### Eviction policy (E2)

`ChunkResidencyManager::beginFrame(vec3 cameraWorldVoxel)` recomputes
`distanceVoxels_` for every slot and marks slots beyond
`R_prefetch + R_hysteresis` as `EVICTING`. `endFrame()` processes
`EVICTING` slots (saves dirty ones via persistence, deallocates pool
slices via the `PoolDeallocator` callback, erases the slot) then
enforces the budget cap — evicting furthest-from-camera slots with
LRU tie-breaking until `residentChunkCount() <= maxResidentChunks_`.

Config knobs on `ChunkResidencyManager::Config`:
- `maxResidentChunks_` (default 256)
- `viewRadiusVoxels_` (default 128.0f — matches the light-volume window)
- `prefetchRadiusVoxels_` (default 256.0f)
- `hysteresisVoxels_` (default 32.0f = one chunk edge — prevents thrashing)

`PoolDeallocator` is the deallocation counterpart to `PoolAllocator`:
production wires it to return the pool slice to `C_VoxelPool`'s
free-list via `deallocateVoxels(startIndex, size)`. The E1 skeleton
leaked allocations on evict; E2 closes that path.

`FrameStats` (via `frameStats()`) reports `evictedThisFrame_`,
`loadedThisFrame_`, and `residentCount_` for HUD display and profiling.

### Chunk mutation must route through `markChunkDirty`

> Any code that writes to a chunk-owned `VoxelPoolAllocation` (the
> slice exposed via `ChunkResidencySlot::poolAllocation_`) MUST call
> `ChunkResidencyManager::markChunkDirty(key)` immediately after the
> write. The same rule covers entity attach / detach / migrate when
> a creation opts into streaming.

The dirty bit is consulted at eviction and by `flushPendingSaves()`;
a missed `markChunkDirty` call after a real mutation means the save
is silently skipped and the chunk reverts to its pre-edit state on
re-resident. This is the ECS-footgun class of bug — invisible under
single-chunk creations, fires only after
streaming load surfaces an eviction-then-re-resident cycle.

`ChunkResidencySlot::isDirty()` is the read side; the underlying
field is private with `ChunkResidencyManager` as a `friend`, so
`slot->dirty_ = true` no longer compiles. The manager's
`attachEntity` / `migrateEntity` already self-route through
`markChunkDirty`; the voxel-mutation routing lands when push-at-
mutation uploads (Epic B / #944) wire in.

When you add a new mutation path (voxel-pool write, entity move,
component write within `ownedEntities_`), route it through
`markChunkDirty`. The cross-link from
[`engine/render/CLAUDE.md`](../render/CLAUDE.md) at the voxel-pool
section is the reciprocal pointer for renderer-side authors.

Most code never touches `World` directly. It accesses managers via the
`IR<Module>::get*Manager()` free functions in each module's `ir_*.hpp`
header, which reach through the global pointers established during
`World` construction (each manager's own ctor stamps its global — see
Responsibilities below).

## Save-trait policy layer (persist P1, #2212, epic #667)

`engine/world/include/irreden/world/save_trait.hpp` declares
`IRWorld::SaveTrait<C>` — a compile-time trait deciding whether component
type `C` participates in a world snapshot, and if so, its schema
`kSaveVersion` (`uint32_t`, lives on the trait, not the component struct —
the snapshot serializes a *schema*, defined by the P2 per-component
serialize function, not the struct's in-memory layout). The primary
template means "no decision yet" (`kExplicit = false`), deliberately NOT
"opt-out" — an engine component that never specializes the trait fails a
compile-time completeness gate instead of silently being skipped by
persistence. Two macros make the decision explicit:
`IR_SAVE_OPT_IN(Type, Version)` and `IR_SAVE_OPT_OUT(Type)`.

`engine/world/include/irreden/world/save_component_inventory.hpp` is the
audited decision table — one `IR_SAVE_OPT_IN`/`IR_SAVE_OPT_OUT` line per
engine component, plus `AllEngineComponents` (a `std::tuple` listing every
one of them) and a `static_assert` that fails the build if any listed
component lacks an explicit decision. It's a heavy include (pulls every
component header) — only world-snapshot TUs and `test/world/save_trait_test.cpp`
should include it, never a widely-included header.

**Opt-out-by-omission is forbidden.** A component with no decision doesn't
silently default to "don't save" — it breaks the build. This is enforced
structurally: the primary `SaveTrait` template has `kExplicit = false`,
and `save_component_inventory.hpp`'s `static_assert` walks
`AllEngineComponents` checking every entry's `kExplicit`.

**New-component contract.** Adding a new engine component requires adding
a matching `IR_SAVE_OPT_IN`/`IR_SAVE_OPT_OUT` line (with its own include)
and an `AllEngineComponents` tuple entry — both the compile-time gate and
`test/world/save_trait_test.cpp`'s `InventoryIsComplete` count backstop
depend on the tuple size matching the audited total. A templated
component with more than one concrete instantiation (e.g.
`C_SystemEvent<SystemEvent>`) gets ONE representative instantiation in
the inventory, not one per specialization — see the inline comment beside
`C_SystemEvent<IRSystem::TICK>` in `save_component_inventory.hpp` for the
rationale (the archetype walk excludes those entities entirely, so the
other specializations are never queried).

P1 is pure metadata — no archetype walk, no IRWS writer, no serialize/
deserialize functions, no Lua surface. P2+ (the `ComponentId → serialize
descriptor` runtime bridge, migration registry, GPU-handle regeneration
pass) consumes `shouldSave<C>()` / `saveVersion<C>()` / `AllEngineComponents`
on top of this layer.

P2 (#2213) added `SaveTrait<C>::kSaveName` (via the `IR_SAVE_OPT_IN/OPT_OUT`
macros — the source spelling of the type, a compiler-stable on-disk key)
and the `saveName<C>()` accessor. It's the CMPN name-table identity; nothing
in P1's decision logic reads it.

## World snapshot — the `IRWS` container (persist P2, #2213, epic #667)

`engine/world/include/irreden/world/world_snapshot.hpp` declares
`IRWorld::saveWorld(registry, path)` / `IRWorld::loadWorld(registry, path)`
— the **entity-level** save path, distinct from `chunk_persistence.hpp`'s
per-chunk `.vxs` **voxel-pool** save (that persists a streaming chunk's
voxel slice, not entities/components; no code overlap). The file is a
standard `engine/asset/` container (magic `IRWS`, `chunk_header.hpp`) with
five chunks — `CMPN` (component name table, the local-index key space),
`ARCH` (archetypes + entity ids + per-column data, each column carrying a
`(saveVersion, byteLength)` header — the P5 migration seam), `SNGL`
(singletons restored by value), `RELN` (relation name table + `CHILD_OF`
edge triples — persist P3, #2214), `META` (nextEntityId watermark). A
write-only `.json` sidecar (Rule #6) rides alongside.

Three collaborating headers:

- `save_serialize.hpp` — `IRWorld::SaveSerialize<C>`, the per-component
  bytes customization point. The primary template is a trivially-copyable
  raw-image fast path (`static_assert`s trivial-copyability); a component
  owning heap storage (`std::string`/`std::vector`/handles) must specialize
  it. P1 decides *whether*; this decides *how*.
- `save_registry.hpp` — `IRWorld::SaveRegistry`, the type-erased bridge.
  `registerComponent<C>()` is `if constexpr`-gated on `shouldSave<C>()`
  (opted-out → no-op, and no `SaveSerialize<C>` instantiation), capturing
  the save-name, version, session-local `ComponentId`, and the erased
  row/singleton read-write hooks. The walker/loader compile once regardless
  of how many components opt in.
- `world_snapshot.hpp/.cpp` — the deterministic projection-merge walker,
  chunk writers, and the two-phase loader.

**Projection-merge + exclusion.** Only registered opt-in components are
written; an entity's saved archetype is the projection of its live
archetype onto them (two live archetypes differing only by a dropped
component merge into one saved archetype). The walker excludes exactly what
`resetGameplay`/`destroyAllExceptPreserved` preserves — singleton entities
(they ride `SNGL`), `C_Persistent`-tagged entities, component-backing
entities — so the load contract

```
IREntity::resetGameplay();          // frame boundary
IRWorld::loadWorld(registry, path); // restores exact original EntityIds
```

is collision-free by construction. Entity IDs are restored **exact**
(never remapped — they never recycle). Same-world double-save is
byte-identical; every read is a recoverable `IRAsset::BinaryStatus` and a
bad magic / truncation / version-too-new / live-id collision aborts with
**zero** world mutation (Rule #5); unknown chunk tags and unresolvable
component names skip with counts.

**P2 scope is the mechanism.** It ships one headless gtest
(`test/world/world_snapshot_test.cpp`) proving the round-trip with
trivially-copyable *test* components. The `(path)`-only convenience wrapper
over a process-default registry (for the P7 Lua binding) has since landed —
see "Process-default registry" below — but only over a **curated subset**;
registering every engine component in `AllEngineComponents` (each needs a
`SaveSerialize<C>` specialization for its non-POD fields) is still downstream
work.

**Relation chunk `RELN` (persist P3, #2214).** `CHILD_OF` entity relations
round-trip through one self-describing chunk: a `Relation`-enum name table at
its head (Rule #2 — the name is the on-disk identity, so adding `OWNS`/
`ATTACHED_TO` is a one-line `enum Relation` extension per Rule #4) followed by
`(relationTypeId, child, parent)` triples. Only logical `CHILD_OF` edges are
serialized — the synthetic relation *entities* (`kEntityFlagIsRelation`) are an
archetype index-space artifact and are regenerated by replaying `setParent` on
load. `PARENT_TO`/`SIBLING_OF` are unimplemented engine-wide, so they name-table
for forward-compat but emit zero triples. A triple is emitted only when **both**
endpoints are in the served set (the same ARCH/SNGL projection P2 wrote), so an
edge to a `C_Persistent`/backing endpoint (recreated with a possibly-new id) is
dropped rather than dangling. Triples are sorted by `(child, parent)` — a child
has one `CHILD_OF` parent — so the double-save stays byte-identical. The writer/
reader live in `src/world_snapshot_relations.cpp` behind the src-private
`src/world_snapshot_internal.hpp` seam (`IRWorld::detail::makeRelationChunk` /
`decodeRelationChunk` / `applyStagedRelations`), keeping the chunk logic off the
public header. Load splits the RELN chunk across two phases to honor Rule #5.
The **fallible parse** — name table, triple count, every triple's bytes into a
staged buffer — runs in **Phase 2b** (`decodeRelationChunk`), alongside the
ARCH/SNGL decode-validate and *before* the id-watermark advance and any phase-3
entity write, so a structurally malformed chunk (bad name table, truncated /
over-stated triple count) aborts the load with the world entirely pristine —
zero entities, zero edges — exactly like a malformed column does. The
**infallible replay** (`applyStagedRelations`) is the **final** load phase —
after every entity, column, and singleton exists and the watermark has advanced,
so `setParent`'s regenerated relation entities mint above every restored id; it
makes no fallible read, so it cannot fail partway and strand a partial edge set.
Endpoints resolve through `LoadResult::singletonAliases_` (identity for a regular
restored entity, the alias for a singleton); an unknown relation name or missing
endpoint skips with a diagnostic (`LoadResult::relationsSkipped_`), never fatal.
Deferring the *parse* to the final phase (which must run after phase 3, since
`setParent` needs the live entities) would leave the whole restored entity set
live on a failed load — the "no partial world mutation on error" contract
(Rule #5) is why only the mutation, not the parse, waits for phase 3.

## Component migration registry (persist P5, #2216, epic #667)

`engine/world/include/irreden/world/save_migration.hpp` declares
`IRWorld::SaveMigration<C>` — the `(component, oldVersion) → reader`
customization point that lets an old `IRWS` snapshot load into a newer build
whose component has since bumped `SaveTrait<C>::kSaveVersion` (Save Format
Extensibility Rule #3). It fills the seam P2 already stamped: every `ARCH`/`SNGL`
column carries a `u32 saveVersion` header, and before P5 the loader **ignored
it** — always reading at the current layout, which silently corrupts an old
column. Now `SaveComponentEntry::readerForVersion(diskVersion)` dispatches four
cases at load, in the mutation-free phase-2 gate (so any failure aborts with a
pristine world, Rule #5):

- **disk == current `kSaveVersion`** — the `SaveSerialize<C>::read` fast path
  (`SaveComponentEntry::reader_`); no migrator lookup. Regresses nothing.
- **disk < current** — the registered `SaveMigration<C>` reader for that
  version, or (miss) a hard `BinaryIOError::MigratorMissing`. This is the **one
  non-recoverable case**: reading old bytes at the current layout can't be
  recovered from, so a known component at an unmigrated older version errors
  out rather than degrading. Every other mismatch degrades gracefully.
- **disk > current** — `BinaryIOError::VersionTooNew` (a future writer),
  reusing the existing shape, naming the component + both versions.
- **unknown component name** — resolves to no registry entry; the column skips
  by byte length (`columnsSkipped_`), never reaching the version dispatch
  (Rule #1 forward-compat, P2 behavior).

**Direct per-version readers, never chained.** Each `SaveMigration<C>` entry
decodes exactly its era's bytes straight to a current-build `C` (fields added
since defaulted, renamed fields remapped); a v3 bump leaves the v1/v2 readers
untouched. The current version is **not** listed in `SaveMigration<C>` —
`SaveSerialize<C>::read` owns it; list only the retired `[1 .. kSaveVersion-1]`.
The registry keys migrators on the current session-local `ComponentId` and the
disk column key stays `SaveTrait<C>::kSaveName` (Rule #2) — never
`typeid(C).name()`, which is compiler-mangled and would make a macOS save
unreadable on Windows. Registration is `if constexpr`-gated on `shouldSave<C>()`
(like `SaveSerialize<C>`), so an opted-out component never needs a migrator.
The `SaveMigration<C>` header block has the worked specialization example; the
generic loader path is proven end-to-end (v1→v2, `VersionTooNew`,
`MigratorMissing`, unknown-skip compose, current fast path) in
`test/world/component_migration_test.cpp`.

## GPU-resident state regeneration on load (persist P6, #2217, epic #667)

`loadWorld` restores CPU-side component data; every component whose
GPU-resident state was opted OUT of serialization (P1 default for handle
fields — textures/SSBOs/framebuffers/pool residency, all process-local)
comes back holding no valid GPU state. **The load contract preserves and
reuses the live render context** rather than reconstructing it: the
`resetGameplay()` that must precede `loadWorld` (`world_snapshot.hpp`)
destroys gameplay entities but keeps the `C_Persistent` canvas / framebuffer
/ camera bundle + `C_VoxelPool` alive (mirroring `scene_reset`, #1857), so
the loader never touches those GPU handles. The one class of gameplay
GPU-resident state that must be rebuilt — `C_VoxelSetNew`'s pool span — is
regenerated by the **canvas-attach seed pass**:

- The saver opts `C_VoxelPool` + every canvas-bundle GPU-handle component
  OUT (`save_component_inventory.hpp` Classes A/B); their contents re-derive
  each render tick from the pool.
- `C_VoxelSetNew` opts IN with a custom `SaveSerialize<C_VoxelSetNew>`
  (`engine/prefabs/irreden/voxel/voxel_set_serialize.hpp`) that persists only
  the canonical `{size, boundsMin, per-voxel records}` and reconstructs the
  set in **staged mode** (`pendingVoxels_` populated, `numVoxels_ == 0`, no
  pool span) — with zero pool interaction, so it is safe in the loader's
  mutation-free phase-2b validate pass (which dry-runs `read`). The "per-voxel
  records" are the *authored* truth, which is not always the live pool span: a
  GRID-mode set saved **mid-rotation** has a derived, dest-lattice-resampled
  span (see `REBUILD_GRID_VOXELS`), so the serializer reads the authored
  snapshot `C_VoxelSetNew::rotationSourceVoxels_` when it is present and the
  span only otherwise — a `saveWorld()` that lands mid-spin still round-trips
  the source arrangement, not the frame's resample.
- After `loadWorld`, the `SEED_STAGED_VOXELS` UPDATE system (or a direct
  `C_VoxelSetNew::attachToCanvas` call) moves each staged set into a live
  pool span and queues its GPU upload. Downstream lighting / AO / sun-shadow
  / fog textures then re-derive from the re-seeded pool for free.

Because `engine/world` must not depend on the voxel/render prefabs (layering),
the seed pass is driven by the **caller**, not baked into `loadWorld`: a
creation that loads a snapshot registers `SEED_STAGED_VOXELS` in its UPDATE
pipeline (see `creations/demos/persist_roundtrip`).

## Process-default registry + `(path)` overloads (persist P7, #2218, epic #667)

`makeDefaultSaveRegistry()` (`src/world_default_registry.cpp`) builds the
process-default `SaveRegistry` the no-registry-argument overloads
`saveWorld(path)` / `loadWorld(path)` forward to — the surface the `IRPersist`
Lua binding (`engine/script`) needs, since Lua passes no registry. It is
**deliberately a curated subset** of `AllEngineComponents`: only components
with a working `SaveSerialize<C>` today — `C_VoxelSetNew` (P6's explicit
serializer, so `voxel_set_serialize.hpp` must be included there) plus
trivially-copyable plain-data components (`C_LocalTransform`,
`C_PositionInt3D`, `C_SizeInt3D`). Registering the **full** inventory does not
compile: the heap-owning opted-in components (`C_Name`, `C_Skeleton`,
`C_MidiSequence`, `C_TextSegment`, ...) still lack a serializer and hit the
primary-template `static_assert`. Add each component to the curated list as its
serializer lands; the full-inventory path (a per-component serializer pass + a
`HasExplicitSaveSerialize<C>` filter over `AllEngineComponents`) is tracked
downstream. **Adding a component here must also extend
`test/script/lua_world_snapshot_test.cpp` with a round-trip case through the
actual `IRPersist` Lua surface** — a standalone `SaveSerialize<C>` unit test
(e.g. `voxel_set_serialize_test.cpp`) covers the serializer but leaves the
wiring itself (registry entry → Lua binding → reload) unverified (#2244). The registry is built **fresh per call** — cheap (a few
allocations, never a per-frame path) and, unlike a process-static, its
session-local `ComponentId`s always match the live `EntityManager`.

The `.json.txt` **debug dump** (W-11) is a second, richer writer over the same
save walk (archetype members + `CHILD_OF` edges), gated by the
`IR_PERSIST_DUMP` env flag (`IRUtility::envFlagSet`) and emitted *after* the
binary — a pure side-output, so the binary is byte-identical flag-on or
flag-off (W-8 parity holds). It is distinct from the always-on lightweight
`.json` sidecar (a magic/version/count summary).

## Responsibilities

`World(const char* configFileName)`:

1. Parses `configFileName` into a `WorldConfig` (resolution, FPS, target
   window size, MIDI device, video-capture defaults, etc.).
2. Constructs every manager in dependency order:
   `IRGLFWWindow` → `LuaScript` → `EntityManager` → `SystemManager` →
   `JobManager` → `InputManager` → `CommandManager` →
   `RenderingResourceManager` → `RenderManager` → `AudioManager` →
   `TimeManager` → `VideoManager`.
   `LuaScript` leads the manager block so `sol::state` outlives
   `EntityManager` — archetype columns can hold `sol::object` refs from
   Lua-defined components (T-100), and C++ destructs members in reverse
   declaration order. `JobManager` slots in after `SystemManager`
   because the worker pool sits below the engine's high-level managers
   (renderer, input, video) and consumes only `WorldConfig` —
   see `engine/job/CLAUDE.md` for the IRJob surface and lifetime
   contract (Phase 1 of the multithreading epic #226).
3. Each manager's constructor stamps its module global as it is
   constructed (`g_entityManager = this;` in `EntityManager`'s ctor, and
   so on) — `World` itself assigns none of them; the member order above
   IS the set order.
4. Calls `initEngineSystems()`, `initIRInputSystems()`,
   `initIRUpdateSystems()`, `initIRRenderSystems()` to register the
   engine-provided prefab systems and assign them to pipelines.
5. Runs any Lua startup scripts the creation registered via
   `IREngine::registerLuaBindings`.

`gameLoop()`:

- Enters the fixed-step outer loop.
- Each iteration: `executePipeline(INPUT)` → `executePipeline(UPDATE)`
  (one or more times, driven by `TimeManager::shouldUpdate()`) →
  `executePipeline(RENDER)`.
- `IRGLFWWindow::swapBuffers()` + frame pacing at the end.

Destructor:

- Destroys managers in reverse declaration order; each manager's
  destructor clears its own module global (if it still points at itself).

## Lua wiring

`setupLuaBindings(std::vector<LuaBindingRegistration>)` is called before
`gameLoop()`. Each registration is a callback that mutates `LuaScript`'s
`sol::state`. Creations use this to register their enum/type/component
bindings before the first Lua script runs.

`runScript(const char* fileName)` loads and executes a Lua file. Bare
filenames resolve from `ExeDir/<ExeStem>/`; paths with a directory component
resolve from cwd.

## Init-affecting runtime params

Runtime parameters that must be applied **before** any manager is
constructed (today: `IRRender::VoxelPoolConfig` sizing, which the
`RenderManager` reads at construction time) live in the same
`config = { ... }` table as the `WorldConfig` fields, in the same
`config.lua`. `IREngine::init` runs a small pre-init pass that loads
the file and applies these fields before constructing the `World`.

The canonical pattern from a creation's side is therefore **nothing** —
the demo's `main()` just calls `IREngine::init(argv[0])` and the
engine handles the rest. The override lives in the creation's own
`config.lua`:

```lua
config = {
    -- ... standard WorldConfig fields (init_window_width, etc) ...
    voxel_pool_edge = 128,   -- override the default 64³ voxel pool
}
```

Missing field → consumer's compiled-in default (`VoxelPoolConfig::kDefaultEdge`
= 64); missing `config` table or missing `config.lua` → same. The pre-init
pass is non-fatal.

**Adding a new init-affecting param.** Extend
`IREngine::detail::applyPreInitLuaConfig` in `engine/engine.cpp` to read
the new field, apply it to its consumer, and log the override at INFO so
startup logs surface non-default values. Document the field here in the
list above and in the consuming module's `CLAUDE.md`. Do not add a CLI
flag for the same purpose — `creations/demos/CLAUDE.md` "No runtime
arguments" forbids it; the Lua config is the single canonical surface.

**vs. `WorldConfig` fields.** `WorldConfig` covers params that World
itself reads at construction time (`init_window_width`, `fit_mode`,
profiling toggles, etc.); the pre-init pass covers params that need to
land **before** `WorldConfig`'s own consumers fire. Both read from the
same `config = { ... }` table — there is one source of truth per file.

## Gotchas

- **Manager lifetime is bounded by `World`.** `g_entityManager` and friends
  are set in the ctor and cleared in the dtor. Don't store references that
  outlive the loop — e.g. a `std::thread` background task that captures
  `g_renderManager` will crash at shutdown.
- **Initialization order matters.** `SystemManager` depends on
  `EntityManager`, `RenderManager` depends on `IRGLFWWindow`, etc. If you
  add a new manager, insert it at the right point in the chain and update
  the dtor order.
- **No `setPlayer` / `setCameraPosition` API on `World`.** Those are ECS
  components. `World` owns *managers*, not game state.
- **`m_waitForFirstUpdateInput` / `m_startRecordingOnFirstInput`** delay
  video recording until the first input arrives — used to keep capture
  clips from starting mid-loading-screen. If video recording is not
  starting, check these flags first.
- **Release GPU/GL resources in `end()`, never in `~World()`.** `g_world`
  is a global `unique_ptr`, so `~World()` runs at process-exit static
  destruction — past that point the GL driver/context may already be torn
  down (MSYS2 unloads it first), and any `glDelete*` issued from a
  member/observer dtor crashes against dead driver state (#2031). `end()`
  runs during `gameLoop()` while the context is live and is the canonical
  spot for device-resource teardown (it already drives `destroyAllEntities()`
  for `onDestroy` GPU frees). The dtor stays a no-op safety net.


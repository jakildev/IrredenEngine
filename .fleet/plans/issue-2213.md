## Plan: persist P2 — IRWS container + deterministic archetype walker + singleton chunk (W-3, W-6)

- **Issue:** #2213
- **Model:** opus
- **Date:** 2026-07-03
- **Epic:** #667 — see `.fleet/plans/issue-667.md` for full context
- **Blocked by:** #2212

### Scope

Deliver `IRPersist::saveWorld(path)` / `IRPersist::loadWorld(path)` in `engine/world/`: the `IRWS` snapshot container (magic + uint32 version + chunk table + JSON sidecar via `engine/asset/` primitives), a deterministic archetype walker that writes opted-in component columns + entity IDs (epic item W-3), and the singleton chunk for `IREntity::singleton<T>()` instances (epic item W-6), plus the type-erased save registry that bridges P1's compile-time `SaveTrait<C>` to runtime archetype columns. Loads restore into a reset world.

Out of scope: relations (P3 — relation pseudo-components are stripped, no relation chunk emitted here), migration registry (P5 — but every column carries the `kSaveVersion` + byte-length header P5 needs), GPU-handle regeneration (P6 — opted-out columns simply aren't written), Lua bindings (P7), full round-trip/determinism test matrix (P4 — this task ships one headless gtest proving the round-trip). Named-entity (`m_namedEntities`) persistence is not in the epic's W-list and is excluded.

Blocked by P1 (`engine/world/save_trait.hpp` with opt-in/out traits + `kSaveVersion`) — verified absent on current master, so this task must not start until P1 merges.

### Verified current state

- **Entity IDs are monotonic and never recycled.** `m_nextEntityId` is an atomic counter starting at `IR_RESERVED_ENTITIES` (0xFF); destroy retires the ID permanently (`engine/entity/src/entity_manager.cpp:55-65`, `:99-105`). Raw ID = low 25 bits (`IR_ENTITY_ID_BITS`, `ir_entity_types.hpp:26-32`). No generation counter — an `EntityId` is index bits + flag bits only. The only flag ever set is `kEntityFlagIsRelation` (`entity_manager.cpp:518`); `IR_PURE_ENTITY_BIT` / `kEntityFlagIsSystem` are declared but never set (repo-wide grep).
- **ComponentIds are session-local entity IDs.** `registerComponentImpl` allocates the ComponentId via `createEntity()` (`entity_manager.cpp:566-572`), keyed by `typeid(C).name()` (`entity_manager.hpp:165-181`) — both the numeric ID and the typeid key are unstable across sessions/compilers, so neither may be the on-disk identity. Rule #2 name tables are mandatory.
- **Archetype walk surface exists.** `EntityManager::getArchetypeNodes()` returns all `smart_ArchetypeNode`s (`entity_manager.hpp:78-80`); each `ArchetypeNode` exposes `type_` (a `std::set<ComponentId>`, already sorted), `entities_`, `components_`, `length_` (`archetype_node.hpp:23-35`). Typed column access is `castComponentDataPointer<C>(...)->dataVector` (`i_component_data.hpp:49-98`). `findCreateArchetypeNode` is graph-public but not exposed through EntityManager (only `findArchetypeNode`, `entity_manager.hpp:75-77`).
- **Singleton internals.** Cache is `m_singletonEntityByComponent` (private, `entity_manager.hpp:522`), lazily validated, cleared by `destroyAllEntities` (`entity_manager.cpp:352-373`) but **not** by `resetGameplay` (`entity_manager.cpp:442-443`). The untyped `getOrCreateSingletonByComponentId` path asserts on C++ components (`appendDefaultRow` returns false — `entity_manager.cpp:446-459`, `:596-599`); only the typed `getOrCreateSingleton<C>` (`entity_manager.hpp:453-466`) works generically. No public accessor for the cache map exists.
- **#663 primitives.** `IRAsset::BinaryWriter/Reader` with file + memory backends, `Result<T>`/`BinaryStatus` recoverable errors (`binary_io.hpp:95-153`, `:208-251`); `writeChunked(w, magic, version, span<ChunkPayload>)` / `readChunks(r, expectedMagic, maxKnownVersion)` with unknown-chunk pass-through (`chunk_header.hpp:88-117`); `NameTableEntry{u32 id_, string name_}` + `NameTable` lookup (`name_table.hpp:38-85`); `JsonSidecarWriter` (write-only, Rule #6); `math_binary_io.hpp` for `IRMath` types. `IrredenEngineWorld` already links `IrredenEngineEntity` + `IrredenEngineAsset` (`engine/world/CMakeLists.txt:7-21`).
- **Fresh-world / teardown interaction.** `resetGameplay` preserves three categories: singletons, `C_Persistent`-tagged entities (camera/framebuffer/canvas stamped at `render_manager.cpp:128-135`), and component-backing entities (`destroyAllExceptPreserved`, `entity_manager.cpp:375-444`); the `C_Persistent` policy lives in the `IREntity::resetGameplay` facade (`ir_entity.cpp:19-27`). `engine/entity/CLAUDE.md` "Save/load implications" already commits #199 to "the entity id round-trips" and notes the singleton cache must rebind on load.
- **Headless test harness exists.** A bare `EntityManager` member is a valid world for gtest (ctor sets `g_entityManager`; `test/ecs/entity_manager_test.cpp:44-56`); world tests register at `test/CMakeLists.txt:90-93`.

### Approach

**Entity-ID decision: restore-exact (raw IDs preserved on load), not remap.** Rationale: (1) epic acceptance #1 explicitly says the serializer "restores … entity IDs" and `engine/entity/CLAUDE.md` already documents "the entity id round-trips" for #199; (2) components embed raw `EntityId` fields (e.g. `C_VoxelSetNew`'s canvas ID) and P2 has no reflection to remap inside opted-in blobs — restore-exact keeps them valid, remap silently breaks them all; (3) W-7 byte-for-byte round-trip parity and P3's relation triples both require stable IDs; (4) the mechanics are cheap because IDs never recycle: restore inserts saved raw IDs into `m_entityIndex`/nodes and advances `m_nextEntityId` past the saved watermark, so post-load allocations can never collide. Collision safety comes from a **save-side exclusion set that mirrors `resetGameplay`'s preserve set**: saveWorld skips (a) singleton entities (handled by the SNGL chunk as value overwrites), (b) `C_Persistent`-tagged entities, (c) relation-flagged and component-backing entities. The documented load contract is `IREntity::resetGameplay()` → `IRPersist::loadWorld(path)` at a frame boundary — everything the save wrote is exactly what the reset destroyed, so same-session load is collision-free *by construction*. Cross-session, load is two-phase (parse + validate everything, then apply): any saved ID colliding with a live entity returns a recoverable `BinaryStatus` error with zero world mutation (Rule #5). A whole-file ID-rebase fallback is left as P5 design space if cross-build infra-ID drift ever bites.

**Singletons load by value, not by ID.** Singletons survive teardown by design, so the SNGL chunk loader resolves each entry by save-name, lazy-creates via the typed `IREntity::singletonEntity<C>()` captured in the registry, and overwrites the live row in place. The saved singleton entity ID is still written and surfaced in `LoadResult` as a `savedId → liveId` alias map (identity in the same-session case) so P3 relation triples referencing a singleton can translate.

**Steps:**

1. **Type-erased save registry** (`engine/world/include/irreden/world/save_registry.hpp` + src). `IRPersist::registerSaveComponent<C>()` — enabled only when P1's `SaveTrait<C>` opts in — captures per component: stable save-name, `kSaveVersion`, `ComponentId` resolver (lazy `getComponentType<C>()`), `writeRow(const IComponentData*, int row, BinaryWriter&)`, `readAppendRow(IComponentData*, BinaryReader&)` (default-construct + trait read; `static_assert` default-constructible), `readIntoEntity(EntityId, BinaryReader&)` and `getOrCreateSingletonEntity()` for the SNGL path. Keyed both by save-name and by resolved ComponentId. Reconcile at claim time: if P1 shipped a runtime registry or `kSaveName` member, consume it; if traits only, this shim is P2's to own.
2. **Minimal EntityManager restore surface** (`engine/entity/`): `findCreateArchetypeNode(const Archetype&)` public wrapper; `restoreEntitiesBatch(ArchetypeNode*, std::span<const EntityId>)` (index emplace + record update + `length_`/`entities_` append + liveEntityCount, main-thread asserted — columns appended afterwards by the registry readers, with an end-of-node sync assert mirroring `entity_manager.hpp:271-275`); `const std::unordered_map<ComponentId, EntityId>& singletonEntityCache() const`; `EntityId entityIdWatermark() const` and `advanceEntityIdWatermark(EntityId)`.
3. **Walker + writer** (`engine/world/include/irreden/world/world_snapshot.hpp` + `src/world_snapshot.cpp`, namespace `IRPersist`, `kWorldSnapshotMagic = "IRWS"`, `kWorldSnapshotVersion = 1`). `saveWorld` flushes structural changes + drains marked deletions first (frame-boundary, main-thread contract), builds the exclusion set, then for every archetype node with `length_ > 0` computes the **projection**: the subset of `type_` that is registered + opted-in (relation pseudo-components and unregistered components drop out here). Nodes with identical projections merge into one saved archetype; merged entity lists sort ascending by masked raw ID (locked order); archetype blocks sort lexicographically by projected sorted-ComponentId set (locked order). Chunks, composed in `MemoryBinaryWriter`s and emitted via one `writeChunked` to a `FileBinaryWriter`:
   - `CMPN` — name table `(componentId as u32, saveName)` for every referenced component (Rule #2).
   - `ARCH` — varuint archetypeCount; per archetype: varuint componentCount + sorted componentRefs; varuint entityCount + ascending varuint raw entity IDs; then per column (same component order): version (`kSaveVersion`, width per P1 — epic W-2 says u32) + varuint columnByteLength + rows written in entity order via `writeRow` (gathering per-entity `(node,row)` for merged groups). The byte-length prefix makes any unresolvable column skippable on load — the seam P5's migration registry plugs into.
   - `SNGL` — varuint count; entries sorted by save-name (ComponentIds aren't cross-session stable; name order keeps the chunk deterministic): componentRef + saved raw entity ID + version + varuint byteLength + value blob. Only cache entries whose component is opted-in and whose entity is live.
   - `META` — `nextEntityId` watermark + total saved entity count.
   - JSON sidecar at `path + ".json"` (summary; write-only, Rule #6), mirroring the `.vxs` pattern.
4. **Loader.** Phase 1: `readChunks` (bad magic / truncation / version-too-new → recoverable error), parse CMPN/ARCH/SNGL/META into staging structs. Phase 2 validate: resolve names against the registry (unresolvable column → counted skip, not fatal; the archetype an entity restores into is the *resolved* set), check every saved raw ID against `entityExists` → collision aborts with no mutation. Phase 3 apply: per archetype `findCreateArchetypeNode(resolvedSet)` → `restoreEntitiesBatch` → per-column `readAppendRow` in entity order (skip unresolvable columns by byte length) → sync assert; SNGL entries via `getOrCreateSingletonEntity()` + `readIntoEntity`, recording the alias map; finally `advanceEntityIdWatermark(savedWatermark)`. Return `LoadResult { status_, entitiesRestored_, columnsSkipped_, singletonsRestored_, singletonAliases_ }`.
5. **Headless gtest** (`test/world/world_snapshot_test.cpp`) + CMake registration + `world_snapshot.hpp` header block documenting the chunk table (Rule #7) + `engine/world/CLAUDE.md` section distinguishing this from `chunk_persistence.hpp`'s per-chunk `.vxs` path.

### Affected files

- `engine/world/include/irreden/world/world_snapshot.hpp` — new: `IRPersist::saveWorld/loadWorld`, `LoadResult`, IRWS format doc block (Rule #7)
- `engine/world/src/world_snapshot.cpp` — new: walker, chunk writers, two-phase loader
- `engine/world/include/irreden/world/save_registry.hpp` + `engine/world/src/save_registry.cpp` — new: type-erased bridge over P1 `SaveTrait<C>` (subject to P1 reconciliation)
- `engine/entity/include/irreden/entity/entity_manager.hpp` + `engine/entity/src/entity_manager.cpp` — restore surface: `restoreEntitiesBatch`, `findCreateArchetypeNode` wrapper, `singletonEntityCache()`, `entityIdWatermark()` / `advanceEntityIdWatermark()`
- `engine/world/CMakeLists.txt` — add new sources (deps already present)
- `test/world/world_snapshot_test.cpp` + `test/CMakeLists.txt` — new gtest
- `engine/world/CLAUDE.md`, `engine/entity/CLAUDE.md` — document the surface + restore contract

### Acceptance criteria

In a headless gtest world (bare `EntityManager`, no window/GPU):

1. **Round-trip:** ≥100 entities across ≥3 archetypes of opted-in test components (including two source archetypes that differ only by an opted-out component, proving projection-merge) → `saveWorld` → `destroyAllEntities` → `loadWorld` → every entity restores with its **exact original raw EntityId**, identical component values, identical resolved archetype membership; `LoadResult.entitiesRestored_` matches.
2. **Singleton chunk:** mutate a singleton value → save → teardown → load → `IREntity::singleton<T>()` returns the saved value; cache stays bound to the live entity; alias map correct.
3. **Opt-out:** a `SaveTrait`-opted-out component on a saved entity does not appear in the file (name absent from CMPN) and is absent after load.
4. **Post-load allocation safety:** `createEntity` after load returns an ID above the saved watermark.
5. **Recoverable failures (Rule #5):** bad magic, truncated-mid-chunk, version-too-new, and a live-ID collision each return a non-ok status with **zero world mutation**; unknown chunk tags and unknown component names are skipped with counts, not errors.
6. **Determinism:** two consecutive `saveWorld` calls on the same world produce byte-identical files.
7. Builds clean on `linux-debug` and `macos-debug`; test runs inside `IrredenEngineTest`.

### Gotchas

- **Never write `typeid` names or raw ComponentIds as identity** — both are session/compiler-local (`entity_manager.cpp:566-572`); the CMPN name table's numeric ID is fallback-only (Rule #2).
- **Mask flags on save:** `entities_` rows carry high-bit flags; write masked 25-bit IDs, skip `kEntityFlagIsRelation` rows defensively (relation entities live in the base node, but belt-and-braces). **This is the P3 boundary contract: relation-entities and archetype-embedded relation ids must never ride the entity/component chunks — relation state is owned solely by P3's relation chunk.**
- **The exclusion set must mirror `destroyAllExceptPreserved`** (`entity_manager.cpp:375-444`): singleton entities, `C_Persistent` entities, component-backing entities. A creation-tagged `C_Persistent` gameplay entity is deliberately *not* saved — document in the header block.
- **`getOrCreateSingletonByComponentId` asserts for C++ components** (`entity_manager.cpp:596-599`) — the SNGL loader must go through the typed lazy-create captured at registration, never the untyped path.
- **Auto-attached `C_LocalTransform`/`C_WorldTransform`:** the restore path bypasses the free `createEntity` wrapper, so nothing is auto-attached — entities restore with exactly the resolved saved archetype. If LT/WT are opted in they round-trip as columns; the walker must not special-case them.
- **saveWorld/loadWorld are frame-boundary, main-thread-only**: flush structural changes + drain marked deletions before walking; assert via the existing `isMainThreadForDeferred` pattern.
- **`kSaveVersion` width:** follow whatever P1 landed (u32 per the epic); don't fork widths in one file.
- **IRMath fields serialize via `math_binary_io.hpp`** helpers, never inline byte-copies (`.claude/rules/cpp-math.md`).
- **No linear searches in the load hot path** (simplify check): key staging lookups by hash maps; the per-node column sync assert catches drift early.
- **Cross-session byte-determinism caveat for P4/W-8:** the locked archetype sort key is the ComponentId set, which is registration-order-dependent — same-session double-save is byte-identical (criterion 6), but a *different* session's re-save may order archetype blocks differently. Flagged to P4 rather than relitigating the locked ordering here.

### Sibling + in-flight reconciliation

- **No open PRs touch `engine/entity/`, `engine/world/`, or `engine/asset/`** (checked 2026-07-03; open PRs are render/tooling only). No merge hazards.
- **P1 files ahead in this chain.** Hard dependency: at claim time, diff this plan against P1's actual surface — trait member names, `kSaveVersion` width, whether P1 shipped a stable save-name and/or a runtime registry. If P1 provides the registry, drop `save_registry.hpp` from this task and consume theirs; if P1 provides no stable name, extending the trait with one is a small additive change to negotiate in the P2 PR.
- **P3 (relations):** this task strips relation pseudo-components from saved archetypes and reserves no tag — P3 adds its relation chunk additively (Rule #1, no version bump). `LoadResult.singletonAliases_` is the hand-off P3 needs for triples referencing singleton entities. P3 also depends on the walker exposing an entity-id resolver hook (identity under restore-exact).
- **P5 (migration):** the per-column `(version, byteLength)` header is the migration seam; P5's `(ComponentId, oldVersion) → reader` registry plugs into the loader's column dispatch without format changes.
- **P6 (GPU regen):** projection-merge means a restored entity may lack opted-out GPU-handle components its pre-save archetype had; P6's regen systems must detect-and-re-add, not assume presence. P6 also defines the `SaveTrait<C_VoxelSetNew>` staged-mode round-trip contract — coordinate.
- **P7 (Lua):** keep `saveWorld/loadWorld` signatures `(const std::string&)` returning status/result structs so the binding is mechanical.
- **`engine/world/chunk_persistence.hpp`** is the *parallel* per-chunk voxel `.vxs` path — no code overlap; the new CLAUDE.md section must keep the two save surfaces clearly distinguished.


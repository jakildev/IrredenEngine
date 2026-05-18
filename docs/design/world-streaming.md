# World streaming + chunked GPU residency (E0 design)

**Status:** Foundation design for Epic E (#938). No code in this PR.
Blocks E1 (chunk identity + residency manager), E2 (prefetch policy),
E3 (per-chunk upload-bandwidth cap + low-LOD fallback), E4 (entity
migration), E6 (disk persistence story shared with the world snapshot).

**Problem owner:** `engine/world/` (residency manager + entity migration),
`engine/render/` (GPU upload + per-chunk pools), `engine/asset/` (disk
persistence reusing `.vxs` infra), `engine/prefabs/irreden/common/` +
`engine/prefabs/irreden/world/` (chunk identity component + chunk-aware
systems).

**Audience for review:** another Opus agent (E0 acceptance criterion).

---

## Why this doc exists now

Today the engine renders one canvas with one fully-resident voxel pool
sized at `kVoxelPoolSize = 64³` voxels (see
`engine/common/include/irreden/ir_constants.hpp:63`). The world bound
collapses to `kChunkSize - 1` on each axis (`kWorldBoundMax`,
`ir_constants.hpp:42`) — i.e. the entire "world" is a single chunk.
`kChunkSize = uvec3{32, 32, 32}` and `C_ChunkVisibleThisFrame` are
declared but unused by any system; the bookkeeping is forward-looking.

Three forces push streaming into the critical path:

1. **VRAM ceilings on real maps.** T-277 (#941) replaces the
   compile-time pool size with a runtime VRAM-budgeted size, but even
   with 8 GB VRAM the `12 B / voxel` `C_Voxel` layout caps a single
   pool at ≈700 M voxels. A 1024³ world is 1 G voxels; a 4096×4096×128
   map is 2 G. Single-pool residency runs out of memory before it runs
   out of design space.
2. **CPU iteration cost on the full pool.** Light-volume seed/propagate,
   AO neighbor sampling, render-cull mask builds all currently iterate
   the full live pool (or its full chunk-bound table). At 1 M live
   voxels the iteration is already measurable in `perf_grid`; at
   100 M it is the frame.
3. **Pre-committed invariants reference chunk streaming.** Both
   invariant #2 (shadow-ring) and invariant #4 (AO guard band) in
   `engine/render/CLAUDE.md` "Lighting culling invariants" call out
   the chunk-streaming activation point as the deferred work. C6
   (#936, rotated-entity grid rotation) also references future
   per-chunk ownership semantics.

E0 is the design pass that locks the data model so E1–E6 can land as
mechanical implementation tasks without rediscovering decisions.

---

## Scope, in one paragraph

A **world chunk** is a `32×32×32`-voxel axis-aligned cube of world
space. The world is paged as a sparse map from chunk-coord `ivec3` to
a residency slot. Each resident chunk owns one `C_VoxelPool`
allocation plus an entity manifest. A camera-driven **residency
manager** decides which chunks are resident this frame, walks a
prefetch ring, evicts stale chunks back to disk, and tickets async
upload jobs against a fixed per-frame upload-bandwidth cap. Chunks
whose upload exceeds the budget render as a **low-LOD AABB billboard**
(decision below) until their full residency completes. Entity
membership migrates between chunks via an atomic, entity-id-stable
ownership transfer routed through the existing deferred-structural-
change queue. Disk persistence reuses `.vxs` HYBRID-mode + `BinaryIO`
chunk-table primitives.

---

## Topic 1 — Chunk identity + addressing

### Coordinate

```cpp
// engine/common/include/irreden/ir_constants.hpp (already present)
constexpr uvec3 kChunkSize = uvec3{32, 32, 32};
```

A chunk is identified by its **chunk-coord** `ivec3 c`. The chunk
occupies the world-space half-open cube
`[c * kChunkSize, (c + 1) * kChunkSize)`. Conversion utilities live
in a new free-function header at
`engine/prefabs/irreden/world/chunk_coord.hpp`:

```cpp
namespace IRPrefab::Chunk {

// Floor toward -infinity so negative coords (e.g. -1.5) land in
// chunk -1, not 0. ivec3 conversion truncates toward zero, which
// would split chunk -1 across coords 0 and -1.
constexpr ivec3 worldToChunk(ivec3 worldVoxel);
constexpr ivec3 chunkOriginVoxel(ivec3 chunkCoord);
constexpr vec3  chunkCenterWorld(ivec3 chunkCoord);

// 64-bit packed key used by the residency map and disk filenames.
// (uint16 sign-extended × 3, 16 unused bits in the high word.)
using ChunkKey = std::uint64_t;
constexpr ChunkKey pack(ivec3 chunkCoord);
constexpr ivec3 unpack(ChunkKey key);

} // namespace IRPrefab::Chunk
```

Three concrete reasons for the `int16`-per-axis pack:

- Covers ±32 768 chunks per axis → ±1 048 576 voxels. Far past any
  current product use case.
- 64-bit key fits in an `unordered_map<ChunkKey, ...>` slot and a
  `std::atomic<uint64_t>` for lock-free residency-set scans.
- Pretty filenames: `world/chunks/-0001_+0042_+0007.vxs` (axis order
  x, y, z, signed four-digit padded — chosen so `ls` sorts the disk
  representation usefully; four digits cover the full ±32 768 chunk
  range without overflow).

### Chunk component on entities

A new pure-data tag component lands alongside `C_PositionGlobal3D`:

```cpp
// engine/prefabs/irreden/common/components/component_chunk_membership.hpp
namespace IRComponents {
struct C_ChunkMembership {
    IRMath::ivec3 chunkCoord_{0, 0, 0};
};
} // namespace IRComponents
```

`createEntity` does **not** auto-attach `C_ChunkMembership` — entities
in single-chunk creations (every existing creation today) carry no
chunk metadata. The residency manager and `system_propagate_chunk_membership`
(see Topic 4) attach the component when an entity first crosses into
streamed territory, and update it on chunk migration. Single-chunk
creations stay zero-overhead; streamed creations opt in by registering
the residency manager and migration system.

The existing `C_ChunkVisibleThisFrame` tag is repurposed: when a
chunk is resident **and** intersects the camera's iso-frustum +
shadow-feeder sweep this frame, every entity inside it gets the tag.
The light-volume seed system can then filter
`<C_LightSource, C_ChunkVisibleThisFrame>` to enforce invariant #3
without a custom viewport check.

### What stays out of chunk identity

- **Subdivision levels (octrees, hierarchical chunks).** Flat
  `ivec3` only. Hierarchical addressing is a Phase 2 question keyed
  to whether streaming-driven LOD materializes (see Topic 4 / LOD
  interactions).
- **Per-chunk rotation.** C6 (#936) introduces rotated-entity grid
  rotation, but rotation lives on the entity (via the SQT
  `C_LocalTransform`/`C_WorldTransform` pair from #731), not on the
  chunk. Chunks are AABB. A rotated entity whose bounding box
  straddles two chunks is tracked by chunk membership of its **root
  entity's world position** — not by per-voxel chunk assignment of
  its skinned mesh.

---

## Topic 2 — Residency manager API

### Where it lives

`engine/world/include/irreden/world/chunk_residency.hpp` +
`engine/world/src/chunk_residency.cpp`. Managed by a new
`ChunkResidencyManager` constructed inside `World` after
`RenderManager` (it needs the device for pool allocation), wired into
`g_chunkResidencyManager` and exposed via `IRWorld::getChunkResidencyManager()`.

The manager does not run as a `System<N>` directly because its work
spans pipeline phases — eviction decisions at frame start, prefetch
ticketing during UPDATE, GPU uploads during RENDER. It exposes named
hooks the engine calls from `World::gameLoop()` between the existing
pipeline phases:

```cpp
class ChunkResidencyManager {
public:
    explicit ChunkResidencyManager(VramBudget budget);

    // Frame hook order — matches gameLoop() integration points.
    void beginFrame(const CameraSnapshot& cam);   // INPUT phase end
    void tickPrefetch();                           // UPDATE phase end
    void flushUploads(int maxBytes);               // RENDER phase begin
    void endFrame();                               // post-present

    // Synchronous API used by editors, save/load, and tests.
    bool isResident(ChunkKey key) const;
    const ChunkResidencySlot* slot(ChunkKey key) const;

    // Forced overrides (editor "load this chunk now" / save-all).
    void requestResident(ChunkKey key, RequestPriority p);
    void requestEvict(ChunkKey key);

    // Entity migration — called from system_propagate_chunk_membership's
    // endTick. Public because the migration system lives in
    // engine/prefabs/irreden/world/, outside engine/world/.
    void migrateEntity(IREntity::EntityId id, ChunkKey oldKey, ChunkKey newKey);

    // Blocks until all pending loads and saves complete.
    // Used by: World construction (warm-up) and save-snapshot path.
    // Covers both "drain all LOADING/UPLOADING slots" and
    // "flush all dirty EVICTING slots" in a single drain pass.
    void flushAllPending();
};
```

### Resident-set representation

```cpp
struct ChunkResidencySlot {
    ChunkKey key_;
    enum class State : uint8_t {
        // Disk → CPU staging buffer in flight.
        LOADING,
        // CPU staging done; GPU upload bytes pending against the
        // per-frame upload budget.
        UPLOADING,
        // Fully resident; pool allocation valid; entities attached.
        RESIDENT,
        // Marked for eviction; pool allocation still valid this frame
        // but no longer in the prefetch ring. Goes EVICTED at endFrame
        // unless re-requested.
        EVICTING,
    } state_ = State::LOADING;

    // Per-chunk GPU pool slice. One allocation per resident chunk.
    // Sized from the disk record's live voxel count, not 32^3.
    IRRender::VoxelPoolAllocation poolAllocation_;
    std::vector<IREntity::EntityId> ownedEntities_;

    // For eviction / prefetch ordering.
    float distanceVoxels_ = 0.0f;
    std::uint64_t lastTouchedFrame_ = 0;
};
```

`std::unordered_map<ChunkKey, ChunkResidencySlot>` is the canonical
store. Cache-line concerns are not a factor at the resident-set
scale (target ≤ 512 chunks resident; the inner per-voxel loops live
in the pool, not the map).

### Eviction policy

Each frame, after `beginFrame` snapshots the camera:

1. Recompute `distanceVoxels_` for every slot as `|cam.worldVoxel -
   chunkCenterWorld(key)|`. This is camera-anchored, identical in
   spirit to the existing light-occlusion grid's camera anchor
   (`engine/render/CLAUDE.md` "Lighting culling invariants — world-
   space").
2. Sort the slots into three buckets by distance vs. the **prefetch
   radius** `R_prefetch` (initial value: `8 * kChunkSize.x = 256`
   voxels — same as the light-occlusion grid window):
   - `dist <= R_view` → must be `RESIDENT` or `UPLOADING`.
   - `R_view < dist <= R_prefetch` → prefetch ring; eligible for
     `LOADING` if budget permits.
   - `dist > R_prefetch + R_hysteresis` → mark `EVICTING`.
3. `R_view = R_prefetch / 2` initially. `R_hysteresis = kChunkSize.x`
   (one chunk of slack) prevents a chunk thrashing between resident
   and evicted as the camera oscillates near the boundary — the same
   pattern `C_VelocityDrag` uses for hover damping.

`EVICTING` slots transition at `endFrame` to a save-to-disk job if the
chunk's `dirty_` flag is set (entities mutated, voxels modified). The
disk write reuses the `.vxs` HYBRID-mode writer; see Topic 6.

**Invariant #2 expansion.** When sun shadows are enabled, the view
radius widens along `-sunDir` by `kSunShadowMaxDistance / kChunkSize.x`
chunks to maintain shadow-ring correctness (the formula from
`engine/render/CLAUDE.md` "Lighting culling invariants" §2). The
shadow-ring expansion is conditional on `IRRender::getSunShadowsEnabled()`,
mirroring `IRMath::shadowFeederIsoBounds`.

**Invariant #4 expansion.** AO sampling requires a `1`-chunk guard
band in all six directions; `R_view` always rounds up to include
`view_aabb.expand(kChunkSize.x)`.

### Async upload job model

Disk → CPU and CPU → GPU run as two dedicated `std::thread` workers
(one disk-bound, one GPU-staging-bound). Each stage has its **own**
SPSC queue — main thread → disk-thread for `LOAD_DISK` jobs, and
disk-thread → GPU-staging-thread for `UPLOAD_GPU` jobs — so each
queue has exactly one producer and one consumer. (A single shared
queue would be SPMC, which is not safe with an SPSC ring.) No
`<concurrent_queue>` standard yet; we ship a small SPSC ring header
in `engine/world/`. Each job carries:

```cpp
struct UploadJob {
    ChunkKey key_;
    enum class Stage { LOAD_DISK, UPLOAD_GPU } stage_;
    std::vector<std::uint8_t> stagingBytes_; // CPU buffer between stages
    // Size in bytes for budget accounting; one frame's flush sums these.
    std::uint64_t sizeBytes_ = 0;
};
```

Completion signals back through `concurrent_queue<JobCompletion>`
drained on the main thread in `flushUploads` so all `IREntity` /
`IRRender` calls remain single-threaded.

---

## Topic 3 — Prefetch policy

Three signals drive the resident-set this frame:

### 1. Camera radius (primary)

The default: every chunk within `R_view` is resident; every chunk
within `R_prefetch` is at least `LOADING`. The radius is camera-
anchored exactly like the light-occlusion grid (`engine/render/
CLAUDE.md` "Lighting culling invariants"). Concrete defaults:

- `R_view = 128` voxels (4 chunks). The light-volume window is 128³
  today; the chunk view radius matches so the lighting and residency
  systems can never disagree about what's reachable.
- `R_prefetch = 256` voxels (8 chunks). One step out from the
  light-volume's camera anchor; large enough to absorb 60 fps × 4 s
  worth of camera motion at typical pan speed before a chunk first
  becomes needed.
- `R_hysteresis = 32` voxels (1 chunk).

These constants live in `chunk_residency_config.hpp` so a creation
can tune them without recompiling the world module. T-277's
runtime-VRAM-budgeted pool sizing feeds into the resident-set
capacity (Topic 4's bandwidth cap) but does **not** shrink the
geometric radius — a sparse world still needs the same look-around.

### 2. Visibility priority (within the radius)

Within the `R_view` ring, chunks that intersect the iso-frustum get
their uploads ticketed ahead of off-screen chunks. The frustum check
reuses `IRMath::shadowFeederIsoBounds` for the AABB (sun-shadow-aware
when enabled). The priority class is just a coarse `enum class
RequestPriority { VISIBLE_RENDER, PREFETCH_RING, FORCED }` —
explicit `FORCED` is the editor "load this chunk now for paint" path
and bypasses budget.

### 3. Velocity hint (future)

The camera entity carries a `C_Velocity3D`. Sliding the view radius
in the velocity direction is straightforward (`R_view_directional =
R_view + cam_speed * predictionHorizon`). Held out of v1 because the
data is ambiguous when the user is mouse-panning vs. mouse-still:
the prefetch ring already covers 4 seconds of motion. Re-evaluate
when a "fast-travel" / teleport / cutscene-camera use case lands —
those are the cases where the static ring miss-rate spikes.

### Cache warm-up

On `World` construction, the residency manager calls
`flushAllPending()` against the spawn camera's resident-set so the
first rendered frame is not staring at a low-LOD wall. The cost is
deterministic: spawn radius is bounded by `R_view` and chunk-load
time is `O(disk_read + voxel_copy)` per chunk.

---

## Topic 4 — Upload-bandwidth cap + low-LOD fallback

### The bandwidth cap

`flushUploads(int maxBytes)` is called by `World::gameLoop()` at the
top of the RENDER phase. Initial budget: `4 MiB / frame` at 60 fps
≈ `240 MiB/s`. The number is tunable per-creation; the default leans
conservative so the GPU upload queue never blocks the render
dispatches. Implementation: drain `JobCompletion`s, sort by
`(priority, distanceVoxels_)`, accept jobs until the cumulative
`sizeBytes_` hits the cap, defer the rest.

The cap is per-frame — a chunk needing 16 MiB of voxel data spreads
across four frames at the default budget. During those four frames
the chunk's render representation is the low-LOD fallback below.

### The low-LOD fallback question (resolution)

The issue body calls out three candidates: **downsampled voxel
proxy**, **SDF silhouette proxy**, **AABB billboard**.

**Decision: AABB billboard for v1, with the migration path to SDF
silhouette proxy reserved for v2.**

| Option | Cost to ship v1 | Visual quality | Lighting correctness | Reuse |
|---|---|---|---|---|
| **AABB billboard** | One `C_ShapeDescriptor` per chunk (BOX SDF, axis-aligned, mean chunk color). Renders through the existing `SHAPES_TO_TRIXEL` path with no shader work. | Reads as a flat-shaded cube. Visible "this chunk is loading" tell — desirable during streaming. | Participates in light-occlusion grid as an `C_LightBlocker` if the chunk records its bounds at save time. | Reuses `C_ShapeDescriptor`, `C_LightBlocker`, `SHAPES_TO_TRIXEL`. |
| **SDF silhouette proxy** | A simpler `C_ShapeDescriptor` derived from the chunk's voxel silhouette — e.g. a single sphere/box CSG approximation baked at save time. | Smoother — silhouette roughly tracks the underlying geometry. | Same as AABB (participates as LightBlocker via bounds). | Same primitives, but adds a bake step at save time. |
| **Downsampled voxel proxy** | Quarter-resolution `8³` `C_VoxelSetNew` per chunk. Allocates a (smaller) pool slot for each loading chunk; needs a second pool kind or a min-allocation knob in `C_VoxelPool`. | Best visual continuity — looks like coarse blocks rather than a placeholder. | Full participation in voxel-pool culling + AO. | Requires `VOXEL_TO_TRIXEL_STAGE_1` to handle two pools or a "low-LOD pass" flag on `C_VoxelPool`. |

The AABB billboard wins for v1 on three grounds:

1. **Marginal renderer surface**: zero new render passes, zero new
   shader work, zero new pool kinds. `C_ShapeDescriptor` already has
   `BOX` as a shape type; one row in `SHAPES_TO_TRIXEL`'s archetype
   iteration covers the entire low-LOD chunk set.
2. **Reads as a placeholder by design**: a flat cube where a complex
   structure should be is an unambiguous visual signal that streaming
   is mid-flight. The voxel proxy and SDF silhouette both look
   "intentional" enough that a slow load looks like a bug.
3. **Forward-compatible**: the chunk record on disk needs a
   per-chunk `aabbColor_` (one `Color`, 4 bytes) and `aabbExtent_`
   (one `ivec3` — really `kChunkSize` minus voxel-content margins,
   so 6 bytes if we use `int16` per axis). Adding an `LSDF` chunk
   later that carries an SDF proxy is a clean extension (Save Format
   Extensibility Rule #1, `engine/asset/CLAUDE.md`).

The `C_VoxelSetNew` downsampled-proxy approach is deferred to v2
explicitly — it requires `C_VoxelPool` to grow either a second
"low-LOD pool" allocation strategy or a multi-pool concept, and that
work belongs in a follow-up after T-277 (#941, runtime-sized pools)
lands. A creation needing "smooth load-in" before that follow-up can
keep its `R_view` large enough that uploads complete in one frame.

### Interaction with the LOD framework (Phase 1, #708)

The streaming low-LOD fallback is **orthogonal** to the artist-driven
LOD tiers in `docs/design/lod-strategy.md`. A chunk's low-LOD
billboard is the engine's "this content is not yet uploaded"
representation. The artist-driven LOD picks between hand-authored
`.vxs` files at different detail levels for fully-resident entities.

When the prefab-manifest LOD (Phase 2 of the LOD design) ships, a
chunk's resident-set will include only the `.vxs` tiers selected by
the current `C_ActiveLodLevel`. The residency manager and the LOD
selector are independent decisions — the manager owns chunk-grain
residency, the selector owns per-entity `.vxs`-tier selection.

### Render-pipeline integration

The chunk-aware version of `system_voxel_to_trixel`'s
`buildChunkVisibilityMask` (already references "chunk visibility"
internally for GPU dispatch buckets, but those are pool-bucket
chunks, not world chunks — see `C_VoxelPool::getChunkCount()` in
`engine/prefabs/irreden/voxel/components/component_voxel_pool.hpp:215-217`)
needs no functional change because each resident chunk owns its own
`VoxelPoolAllocation` and the per-allocation iso-bounds drive the
existing cull. The naming overlap between "world chunk" and "GPU
dispatch chunk" is unfortunate but bounded: in the streaming code
"chunk" always means `kChunkSize`-cube, and the GPU dispatch
bucket retains the name `kVoxelChunkSize` (its constant in
`engine/render/include/irreden/render/ir_render_types.hpp`).

---

## Topic 5 — Entity migration semantics

### The invariant

> When an entity's world position crosses a chunk boundary, its
> chunk membership updates atomically at a frame boundary, its
> EntityId stays stable, and every system observes a consistent
> `(entity, chunkCoord)` pair within a single tick.

Specifically:

- EntityId is preserved through migration. No destroy/recreate.
  Lua references, parent/child relations, and stored EntityIds in
  game state continue to resolve.
- Migration runs as a deferred structural change at the end of the
  UPDATE pipeline, before `RENDER` ticks. Mid-tick mutation is
  unsafe (per the existing `flushStructuralChanges` contract,
  `.claude/rules/cpp-ecs.md` "Deferred entity operations during
  tick").
- A migration adds the entity to the destination chunk's
  `ownedEntities_` and removes it from the source chunk's. If the
  source chunk is non-resident at the time of migration, the
  migration is queued and applied when the source chunk
  re-enters residency. (See "Edge cases" below.)

### Migration system

```cpp
// engine/prefabs/irreden/world/systems/system_propagate_chunk_membership.hpp
// Pipeline: UPDATE, registered after PROPAGATE_TRANSFORM (which writes
// C_WorldTransform; this system reads world position) and before any
// chunk-aware downstream system.
namespace IRSystem {
template <> struct System<PROPAGATE_CHUNK_MEMBERSHIP> {
    // Per-entity tick: reads C_WorldTransform.translation,
    // converts to chunk-coord, compares to current C_ChunkMembership,
    // appends to migrations vector if changed.
    void tick(const C_WorldTransform& world, C_ChunkMembership& mem);

    // endTick: flushes the migration vector through
    //   ChunkResidencyManager::migrateEntity(id, oldKey, newKey).
    void endTick();

    std::vector<MigrationRecord> migrations_; // params-on-System

    static SystemId create() {
        return registerSystem<PROPAGATE_CHUNK_MEMBERSHIP,
                              C_WorldTransform,
                              C_ChunkMembership>("PropagateChunkMembership");
    }
};
} // namespace IRSystem
```

The `migrations_` vector is `reserve`d once in the params ctor; the
per-entity tick `push_back`s into it. In high-migration scenes (mass
projectile flight) the vector grows; capacity is retained across
frames per the "Allocations in hot tick paths" rule in
`.claude/rules/cpp-ecs.md`.

### Inter-chunk parent/child relations

The CHILD_OF graph (the `Relation::CHILD_OF` used by
`PROPAGATE_TRANSFORM` for SQT, by the joint system, by gizmo anchors)
is **not** chunk-scoped. A parent in chunk A may have a child in
chunk B; both are independent residency citizens. The transform
propagation only requires that both entities exist in the resident-
set at the time `PROPAGATE_TRANSFORM` runs — it walks parent chains
by EntityId, not by chunk locality.

If the parent's chunk is not resident, the child reads identity for
its parent transform (the same fallback `PROPAGATE_TRANSFORM` uses
for missing-`C_WorldTransform` parents, per `engine/prefabs/
irreden/common/CLAUDE.md` "SQT transform pair + propagation"). This
is a graceful degradation — the child renders at its local-relative
position; the visual is wrong only for the frame(s) the parent is
streaming in.

A v2 follow-up could promote cross-chunk parent chains to a
"pinned" set that drags the parent's chunk into the resident-set
whenever any child is visible — but the failure mode (parent chunk
non-resident, child snaps) is recoverable enough to defer.

### C6 (#936) — rotated-entity grid rotation

C6's rotation operates on entity-local space (the `C_LocalTransform`
quaternion). A rotation does **not** change which chunk an entity
belongs to — chunk membership is decided by the world-space root
position. A rotated entity whose bounds straddle two chunks remains
single-chunk for residency purposes; the renderer sees the rotated
geometry via the same `C_WorldTransform` it always does.

The interaction worth flagging: a long thin rotated entity's
**lighting-blocker bounds** (`C_LightBlocker`) may extend outside
its root chunk's AABB. The light-occlusion grid build needs the
expanded bounds, not the chunk AABB. This is already true today
(grid build iterates the pool, not chunk bounds — `engine/render/
CLAUDE.md` "Lighting culling invariants" §1) and stays true under
streaming.

### Edge cases

1. **Entity teleport across many chunks in one frame.** The
   migration system handles arbitrary distance — it computes
   `worldToChunk(worldPos)` directly, not by neighbor walk. The
   destination chunk's residency is checked synchronously; if
   non-resident, a `requestResident(destKey, FORCED)` is issued
   and the migration applies on the next frame when the chunk
   becomes resident.

2. **Entity destroyed mid-migration.** The migration record carries
   the EntityId; if `IREntity::destroyEntity(id)` fired this frame,
   the destruction's `removeBySource(id)` sweep (per the modifier
   framework's pre-destroy hook) is the canonical signal — the
   migration system's `endTick` checks
   `IREntity::entityExists(record.entityId)` before applying.

3. **Source chunk evicted while a child entity's migration is
   pending.** The pending migration is dropped. The entity was
   serialized to disk with the source chunk at eviction time; its
   `EntityId` persists in the saved chunk file. When the source
   chunk reloads, the entity reappears at its original
   pre-migration position. (Eviction saves the whole chunk, not
   individual entities — see Topic 6.)

4. **Concurrent migration + structural change.** All structural
   changes route through `flushStructuralChanges` at pipeline end.
   Migration is the *last* structural change applied each frame
   so it sees a settled archetype graph.

---

## Topic 6 — Disk persistence

### Reuse, not reinvent

The asset module's binary-I/O primitives (`engine/asset/CLAUDE.md`)
already provide every primitive a streaming chunk needs:

- `BinaryWriter` / `BinaryReader` with `Result<T>` error returns.
- `writeChunked` / `readChunks` chunk-table-based file framing with
  silent-skip-unknown forward compatibility (Save Format Extensibility
  Rule #1).
- `.vxs` HYBRID-mode `saveVoxelSet(path, shapeRecords, denseVoxelSet)`
  / `loadVoxelSet(path)` already covers the two payload types a chunk
  can carry (SDF shapes + dense voxel records).
- `NameTable` for `ShapeType` / future `ComponentId` enums that
  travel by name + id (Rule #2).
- Per-record `kSaveVersion` versioning (Rule #3).
- JSON sidecar regeneration on save (Rule #6).

The streaming chunk file is **`.vxs` HYBRID + new optional chunks**.
No new magic, no new top-level format. The HYBRID container's `BNDS`
and `VOXR` chunks carry the dense voxel data; the `SHPG` chunk
carries any per-shape SDF primitives the chunk contains; new chunks
get added per Rule #1:

```
World chunk file: <world>/chunks/<x>_<y>_<z>.vxs
Magic: VXS1 (existing)
Version: 1 (existing)
Chunks:
  MODE = HYBR              (existing, asset-module driven)
  SREF                     (existing, shape-type name table)
  SHPG                     (existing, SDF primitives in this chunk)
  BNDS                     (existing, dense AABB)
  VOXR                     (existing, dense voxel records)
  ENTS = entity manifest   (NEW — Topic 6.2 below)
  BBOX = low-LOD billboard (NEW — Topic 4 fallback metadata)
```

### Chunk record (`ENTS` — entity manifest)

The chunk records which entities are "owned" by the chunk, with
enough state to recreate them on load. The world-snapshot work in
`engine/world/` (issue #199 — referenced from `engine/asset/CLAUDE.md`)
is the canonical home for the entity-state encoder; per-chunk
`ENTS` reuses it. The chunk persistence does NOT define a new
component-state encoder — it defers to the snapshot module's
encoder, which addresses Rule #3 (per-component versioning) and
Rule #4 (relations as first-class data).

Concretely, the `ENTS` chunk body is:

```
varuint entityCount
repeat entityCount times:
    uint64 chunk-local-entity-id  (stable within the chunk file)
    varuint componentCount
    repeat componentCount times:
        uint16 componentTypeId    (via name table)
        uint16 componentVersion   (per-component additive)
        varuint blobBytes
        uint8[blobBytes] componentBlob
    varuint relationCount
    repeat relationCount times:
        uint16 relationTypeId     (via name table)
        uint64 otherChunkLocalId  (or absolute id if cross-chunk — flag bit)
```

The world snapshot (#199) is the system of record for "how an
entity serializes." The chunk file is a sliced view — entities
inside this chunk's voxel AABB plus the components/relations
needed to bring them back. Cross-chunk relations (entity in
chunk A with a CHILD_OF link to entity in chunk B) are stored
with a flag bit + absolute EntityId; the loader resolves them
lazily after both chunks become resident.

This sharing decision lands at the same time as the snapshot
work, so a chunk file IS a slice of a snapshot, not a separate
format. Phase 1 of the chunk-persistence task implements the
slicing logic against the in-flight snapshot encoder; if the
snapshot lands first (likely, since #199 is on the queue
already), the chunk-persistence task is mostly the slicing +
loader.

### Chunk record (`BBOX` — low-LOD billboard metadata)

```
uint32 packedRGBA     (Color::toPackedRGBA — chunk's mean color)
ivec3  aabbMinVoxel   (relative to chunk origin)
ivec3  aabbMaxVoxel
uint8  flags          (bit 0: blocks light, bit 1: emissive proxy, ...)
```

Tiny chunk (≈ 32 bytes per chunk file) — bakes at save time from the
chunk's resident voxel data so the loader can build the low-LOD
billboard without parsing the full `VOXR` chunk. The load order:

1. Read header + chunk table only.
2. Read `BBOX` immediately and instantiate the low-LOD billboard
   entity. The chunk is now visually "present" with one frame's
   latency.
3. Schedule `VOXR` + `ENTS` decode as the upload-budget allows.

This is what makes the low-LOD fallback an actual streaming
behavior, not a placeholder hack — the billboard appears
synchronously with the chunk-table read, then is replaced by the
full content as the upload budget catches up.

### Dirty tracking + eviction-write

Each `ChunkResidencySlot` carries a `dirty_` bit. The bit is set by:

- Any voxel mutation to a voxel owned by the chunk's
  `VoxelPoolAllocation`.
- Any entity creation, destruction, component change, or migration
  affecting an entity in the chunk's `ownedEntities_`.

The bit is consulted at `EVICTING → EVICTED` transition: if dirty,
schedule a save to disk before deallocating the pool slice. The
save is async (the residency worker pool), but the pool slice
holding the data must persist until the save completes — which
is why `EVICTING` is a state separate from `EVICTED`.

Pristine chunks (loaded but never modified) skip the save. A
read-only world (e.g. a published level) never writes to disk.

### Save-all path

`ChunkResidencyManager::flushAllPending()` walks every resident
slot, forces a save on every dirty one (synchronously this time —
this is invoked from the save-snapshot button), and returns when
all jobs complete. The world-snapshot save (when #199 ships)
calls this path before serializing the rest of the world state.

### File layout

```
saves/<save-name>/
    snapshot.bin            # world-level state (issue #199)
    chunks/
        +0000_+0000_+0000.vxs
        +0000_+0000_+0000.vxs.json   # sidecar (Rule #6)
        -0001_+0000_+0000.vxs
        ...
```

One file per chunk. Filenames as in Topic 1. The sidecar is
write-only, regenerated each save (Rule #6). Per-chunk file
granularity is a deliberate choice — it makes partial saves
trivial (only dirty chunks rewrite), it makes resume-from-eviction
cheap (read one file, not the whole world), and it composes with
Git for source-controlled test fixtures.

A 4096³-world worst-case would be `~2 M chunk files`. Filesystem
considerations:

- Two-level directory split: `chunks/<x_div_64>/<y_div_64>/...`.
  Caps any one directory at `4096` files. Sketched here, decision
  deferred to the implementation pass (filesystem behavior varies
  enough that the right answer needs measurement on the target
  platforms).

---

## Migration story (single-chunk → multi-chunk)

Existing creations are single-chunk by definition (one canvas, one
pool, `kWorldBoundMax = kChunkSize - 1`). The transition lands per
creation, not engine-wide:

1. **No opt-in: zero overhead.** A creation that doesn't construct
   `ChunkResidencyManager` sees no behavioral change. The auto-attach
   chain stays exactly as today; `C_ChunkMembership` is never added;
   `PROPAGATE_CHUNK_MEMBERSHIP` is never registered.

2. **Opt-in: explicit registration.** A streamed creation constructs
   the residency manager via `IRWorld::enableChunkStreaming(config)`
   in its setup. That call attaches `C_ChunkMembership` to every
   live entity (whose `C_WorldTransform` is read once to compute
   the initial coord), registers the migration system in UPDATE,
   and starts the residency manager's first frame at the camera's
   spawn coord.

3. **No global flip-day.** The single-chunk path remains supported
   indefinitely. The "all worlds are streamed" decision is a creation-
   level choice, not an engine deprecation.

---

## What this design deliberately does NOT decide

These are the things the implementation team has authority to lock
when the corresponding ticket lands; they're called out here so
nobody re-imports the decision unexpectedly:

- **Concrete VRAM-budget math.** T-277 (#941) lands first; its
  budget telemetry is the input the residency manager queries to
  pick `MAX_RESIDENT_CHUNKS`. The arithmetic
  `(budget_bytes - kRenderOverhead) / (kChunkSize.x³ * 12 B/voxel)`
  is straightforward; the overhead numbers depend on what else
  T-277 has reserved.
- **Worker-thread count.** v1 starts with 2 (one disk-bound,
  one GPU-staging-bound). If profiling reveals contention, scale
  to `min(4, hardware_concurrency / 2)`.
- **Eviction LRU vs. distance.** Default to distance; promote to a
  hybrid (distance with LRU tie-break) if profiling reveals
  thrash near the prefetch boundary that hysteresis doesn't catch.
- **Disk format compression.** The `.vxs` HYBRID format does not
  compress today. Adding `zstd` is a follow-up — every chunk
  payload is independent so per-file compression is the obvious
  shape. Hold for v2; the upload budget is bytes-per-frame, not
  decompressed-voxels-per-frame.
- **Multi-pool vs. per-chunk-allocation.** v1 uses a single
  `C_VoxelPool` shared across all resident chunks, with each chunk
  owning a contiguous allocation inside it. If pool fragmentation
  becomes the bottleneck (a few large chunks evicting and reloading
  leaving gaps), v2 switches to one pool per chunk. The
  `VoxelPoolAllocation` interface already encapsulates the
  caller-visible surface, so this is an internal change.
- **Iso-spatial-hash inclusion.** `engine/prefabs/irreden/render/
  iso_spatial_hash.hpp` is a stub that could feed
  `ChunkResidencyManager::beginFrame` to skip chunk distance
  recomputation for non-moving cameras. Defer until profiling
  shows the recompute cost matters — the resident set is ≤ 512
  chunks; distance recompute is sub-millisecond.

---

## Acceptance trace

Maps the issue's acceptance criteria to sections above:

| Criterion | Where covered |
|---|---|
| (1) Reviewed by another Opus agent | This PR's review (E0 acceptance) |
| (2a) Chunk identity + addressing | Topic 1 |
| (2b) Residency manager API | Topic 2 |
| (2c) Prefetch policy | Topic 3 |
| (2d) One-frame upload-bandwidth cap + low-LOD fallback | Topic 4 |
| (2e) Entity migration semantics | Topic 5 |
| (2f) Disk persistence story | Topic 6 |
| (3) Low-LOD representation resolved | Topic 4 — AABB billboard, with SDF silhouette deferred to v2 |
| (4) No code changes | This PR adds only `docs/design/world-streaming.md` |

---

## Follow-up tasks this design unblocks

E1 — chunk identity + residency manager skeleton (`engine/world/
chunk_residency.{hpp,cpp}` + `engine/prefabs/irreden/world/
chunk_coord.hpp` + `C_ChunkMembership` component). Opus.

E2 — prefetch policy + camera-anchored radius + visibility priority
queue. Opus (depends on E1, T-277).

E3 — async upload pipeline + bandwidth cap + low-LOD billboard
spawn. Opus (depends on E1).

E4 — entity migration system + cross-chunk relation handling.
Opus (depends on E1, snapshot encoder shape from #199).

E6 — disk persistence (chunk slicer for snapshot encoder + load
path). Opus (depends on #199 landing the snapshot encoder).

Each task's plan can cite the relevant Topic of this doc rather than
re-reading the whole context graph that produced these decisions.

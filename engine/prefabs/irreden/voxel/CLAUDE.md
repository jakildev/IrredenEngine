# engine/prefabs/irreden/voxel/

Voxel pools, owned voxel-set spans, SDF shapes, grid rotation, and skeletal
binding. API contracts live in the headers; this file owns cross-header and pipeline constraints.

## Pool and voxel-set contracts

- A `C_VoxelPool` belongs only on a canvas entity, one pool per canvas.
  `C_VoxelSetNew` owns one contiguous span in that pool and captures its target
  canvas at construction. Detached entity canvases, absent from the render
  manager's named-canvas map, use the entity-keyed `IRPrefab::VoxelPool` facade.
- `C_Voxel` is a 12-byte std430 GPU record. Its alpha defines activity, and
  its face-occlusion and `reserved_` bits are shader-visible encodings. When
  assigning a `reserved_` bit, update the layout comment and every shader
  mirror in the same change; the compiler cannot detect a collision.
- The pool active mask must mirror voxel alpha. Use the set's bulk mutators
  or `editVoxels` / `carve`; they restore the rotation-source mirror, active
  mask, and face occupancy in that order. For a multi-pass raw-span edit,
  write everything and call `resyncAfterRawEdits()` once; never hand-roll the
  pair. `syncActiveMask()` remains only for existing low-level raw-loop sites.
- `visible_` is a transient whole-set render gate: hiding clears the mask but
  preserves authored alpha; showing reconstructs it; both update arms skip
  hidden sets. Fog's BODY carrier (bit 3 + factor bits 11:4) and the rotated
  silhouette-riser bit are independent and must survive the rotation-source snapshot.
- Color mutations made while GRID rotation is active must also reach
  `rotationSourceVoxels_`; the identity frame restores that source span and
  clears the snapshot. Direct raw-span writes are safe only before the first
  rotated frame unless they use the encapsulated edit API.
- `C_VoxelSetNew` reserves its span during construction when a canvas is
  active; a temporary made only to inspect dense data leaks because cleanup is
  `onDestroy()`, not a destructor. Attach it to an entity or use
  `DenseVoxel::toVoxels`; destroy entities through `IREntity::destroyEntity`.

## Entity anchors

`C_VoxelSetNew::anchor_` is baked into local voxel positions, so raster,
render, culling, occupancy, and picking need no anchor branch:

| Anchor | Local origin | Contract |
|---|---|---|
| `CORNER` | `(0,0,0)` | Translation is the minimum corner; legacy default. |
| `CENTER` | `-(size-1)*0.5` | Solid is centered on its entity origin. |
| `GROUND` | centered XY, `-(size.z-0.5)` in Z | Discrete entity position is footprint center at body bottom. |

New discrete-entity prefabs use `GROUND`: at `translation.z == floorSurfaceZ`
they stand flush for every size, so perception, arrival, fog, spawn, and UI
consumers use the entity position without a height correction. Existing
content changes anchor only through a deliberate migration. Code
reconstructing the body's center must call `anchorLocalCenter(anchor_, size_)`,
never assume the CORNER formula. The anchor does not implicitly migrate
collider, SDF-shape, or entity-canvas geometry; each adopts it separately.

Detached revoxelization rotates about the pool origin and therefore requires
one centered set per private pool. Its rebuild tick asserts that composed
locals are symmetric about the origin before seeding the static cull bound;
the check catches any off-center authoring mode, not merely `GROUND`, and
half-cell-anchor uniformity does not prove the pivot is centered. The guard
belongs at the rebuild consumer, the one site holding both set and pool.

Lua exposes anchors as integer enum values. The three-argument voxel-set ctor
takes `(size, color, anchor)`, no legacy boolean arm; the four-argument form
appends an explicit target canvas (headless construction, detached canvases).

## Transform and revoxelization pipeline

- `UPDATE_VOXEL_SET_CHILDREN` is translate-only: it copies the composed
  `C_WorldTransform.translation_` into pool globals. Any transform writer,
  modifier resolver, and `PROPAGATE_TRANSFORM` must run before it or visible
  voxels lag one frame. Visible sets upload every frame; only the shared
  shadow-feeder cull gate skips work. Its parallel workers write disjoint spans
  and merge queued-position ranges in `endTick`.
- `REBUILD_GRID_VOXELS` runs after that baseline update for explicit GRID
  entities. It walks destination cells and inverse-maps them into source
  occupancy (forward scatter is not surjective and leaves rotation holes).
  Covered-cell overflow drops interior cells first so the visible surface is
  retained; source-cell aliasing is deterministic first-wins in authored order.
  Rotated output carries `VoxelReserved::kRotatedEmit` for the render
  pipeline's silhouette-riser face rule.
- `REBUILD_GRID_VOXELS_IMPLICIT` handles entities with no `C_RotationMode`
  (absence means GRID everywhere else); register it right after the explicit
  system. Their archetypes are disjoint; the twin delegates to the same code.
- Creations using GRID entities register both rebuild arms after
  `UPDATE_VOXEL_SET_CHILDREN`. On-screen rotation re-rasterizes every frame;
  transform comparison is a forbidden dirty flag. Extend
  `test/ecs/grid_rotation_test.cpp` whenever adding public grid-rotation math.
- DETACHED entities bypass GRID rebuild and rotate through their canvas TRS.
  `REBUILD_DETACHED_VOXELS`, after `PROPAGATE_CANVAS_ROTATION`, only seeds the
  pool's conservative origin-centered static bound once. The GPU compute owns
  per-frame revoxelization and uses the same round-half-up inverse mapping as
  CPU GRID rotation. The private pool must remain one centered set; a future
  multi-set or off-origin design needs per-set pivots and bounds. The static
  bound is rotation-independent, but the pool still culls when the camera
  pans away from its canvas origin.
- Temporal spin flicker is an accepted lattice-resampling limitation: occupied
  cells change discontinuously at half-integer crossings. Do not add sticky
  cells or hysteresis, which cache last-frame state as a dirty flag.

Any UPDATE system that writes `pool.getPositionGlobals()` must cull with
`IRPrefab::SunShadow::shadowFeederCullViewport`, not the narrower render
viewport. The shadow bake reads the widened area; using the render viewport
freezes off-screen casters' pool positions and therefore their shadows.

## Cull-bounds invalidation

`C_VoxelPool` derives a cardinal iso/depth cache and a yaw-independent world
AABB cache from the allocated prefix, voxel alpha, and voxel global position.
Both caches share one range evictor:

```cpp
pool.markCullBoundsDirty(startIndex, count);
IRPrefab::VoxelPool::markCullBoundsDirty(startIndex, count, canvasEntity);
```

Call it after any in-place alpha or position rewrite of an owned span.
Invalidation is per voxel chunk; overlapping ranges coalesce and a zero count
is a no-op. The caches keep independent pending bits because their consumers
can run on different frames and neither may consume the other's work.

Allocation, deallocation, queued-position uploads, active-mask mutations, and
the voxel-set mutators already invalidate. A set-level alpha mutator must
invalidate even while `visible_ == false`: visibility suppresses the mask,
whereas bounds derive from authored alpha.

Detached GPU revoxelization is exempt as a producer because its static bound
does not read the stale CPU mirror. Its static-bound branch is still a cache
consumer and must clear the pending cardinal state, or `isRangeVisible` admits
every detached range forever: a branch answering a cache query owns that
cache's pending state even when it derives the answer elsewhere.

UPDATE movers run before render rebuilds these caches. A pending range is
therefore admitted conservatively for one tick; answering from stale bounds
could keep an edited off-screen set permanently latched out. Validate changes
with `test/ecs/chunk_bounds_eviction_test.cpp` and
`IRShapeDebug --auto-screenshot --cull-evict-test`.

## Prefab.spawn voxel_ref → ECS components

- Primary entities use DENSE `.vxs` data. SHAPES are effects-only SDF
  entities, and HYBRID is backward-compatible load-only. Shape records become
  parented children whose local transforms are composed by
  `PROPAGATE_TRANSFORM`; fields with no renderer consumer remain unattached.
- `C_ShapeDescriptor` renders directly on the GPU and allocates no voxels. It
  snapshots the active canvas with the nullable accessor so headless prefab
  construction remains valid. Fog owns `SHAPE_FLAG_FOG_HIDDEN` independently
  of the author-owned visibility bit and folds `fogBodyFactor_` into GPU flags.

## C_VoxelSetNew headless / staged mode

- The dense-data constructor stages records when no canvas exists
  (`numVoxels_ == 0`, `canvasEntity_ == kNullEntity`); a staged set neither
  writes nor deallocates a pool span. `SEED_STAGED_VOXELS`, registered before
  `UPDATE_VOXEL_SET_CHILDREN`, calls `attachToCanvas` until that state clears.
- Attach resolution is explicit canvas, then the set's saved canvas, then the
  active canvas; a successful attach queues positions for the first render
  frame. Allocation mismatch becomes a clean nonresident no-render state; do
  not retain pending data for a futile per-frame retry that could later seed
  uninitialized slots.
- Destroying a canvas re-stages every set resident in its pool instead of
  stranding it: an `EntityManager`-scoped pre-destroy hook
  (`voxel_pool_teardown.hpp`), armed by attaching a `C_VoxelPool` — a new
  pool-creation site must arm it too — returns each set's records and origin
  to the staged state and frees its span while the pool is still alive, so the
  seed pass re-homes it. Re-targeting is never an option because span indices
  are pool-relative. The seed rebuilds span-derived state rather than carrying
  it: priority-tier counts, the GPU transform slot (a freed span resets to
  static), and bone slots.
- `destroyEntity` only marks, so the re-stage lands at the
  `destroyMarkedEntities` drain; a test that destroys a canvas must drain first.

## Entity-based joints

`C_Skeleton::joints_` is the canonical ordered bone-id space: a voxel's
`bone_id` is the vector index. Each `C_Joint` is an entity with a local
transform and `CHILD_OF` relation, so ordinary transform propagation composes
the bone tree and joints may carry gameplay components. Voxel skinning maps
each bone to its per-voxel entity-transform slot; the separate joint-transform
buffer remains SDF-shape scaffolding.

Indices stay stable across saves and eventual severance. Severing (designed,
not yet implemented) must orphan the joint subtree, bake affected voxels into
a free-flying set, and replace the skeleton slots with `kNullEntity` rather
than splice the vector.

`bindPose_` is parallel to `joints_` and stores rest transforms in rig-root
local space, from the rig joint chain rather than the asset BIND chunk; BIND
holds named attachment points surfaced through `C_BindPoints`. Bind-point name
lookup and `Skeleton::skinMatrix`'s entity-reading overload are one-time
spawn/editor/interaction operations, never per-tick queries. `C_JointName` is
an optional editor/animation key; vector order remains authoritative.

## Deprecated

| Surface | Replacement |
|---|---|
| `C_JointHierarchy` | `C_Skeleton` + joint entities |
| `C_VoxelPool::markChunkWorldBoundsDirty()` | `markCullBoundsDirty(start, count)` |
| `C_VoxelPool::markChunkBoundsDirty()` | `markCullBoundsDirty(start, count)` |

The compatibility forwarders invalidate both caches over the full allocated
prefix; out-of-tree callers should migrate to the range form.

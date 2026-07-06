# engine/prefabs/irreden/voxel/ — voxel pools, sets, shapes

Voxel rendering building blocks: pools, voxel sets (owned spans of a
pool), shape descriptors (SDF-based, GPU-resident), and entity builders
for single voxels and particles.

## Key components

- `C_Voxel` — per-voxel record (12 B std430): RGBA color + `material_id`,
  `flags` (bit-packed: bit 0 `kAoContrib`, bit 1 `kEmissive`, bits 2..7
  face-occlusion bits `kFaceOccluded{Neg,Pos}{X,Y,Z}`), `bone_id`,
  `layer_id` (editor layer membership; 0 = default layer). Default ctor
  sets `flags = kAoContrib` and the rest zero so v1 scenes render
  unchanged. Face-occlusion bits are maintained by
  `IRPrefab::Voxel::recomputeFaceOccupancy` (`voxel/face_occupancy.hpp`),
  invoked from set-level `C_VoxelSetNew` mutators (`reshape`, `fillPlane`,
  `activate/deactivateAll`, dense-data ctor). Voxels usually handled as
  spans inside a `C_VoxelPool`. Layout matches the GPU SSBO at slot 6 —
  see `components/component_voxel.hpp` and the per-pipeline shaders for
  the struct mirror.
- `C_VoxelPool` — master allocator; allocates/deallocates contiguous spans,
  tracks per-chunk bounds for visibility culling, and owns a per-slot
  active-mask (`m_activeMask`) that mirrors `m_voxelColors[i].color_.alpha_ != 0`.
  The mask is uploaded to slot `kBufferIndex_VoxelActiveMask` each frame
  and read by `c_voxel_visibility_compact.{glsl,metal}` in place of the
  per-voxel alpha test (T-287). Push-at-mutation: mutations through
  `C_VoxelSetNew`'s helpers sync the mask automatically. **One pool per
  canvas entity.**
- `C_VoxelSetNew` — owns a span of voxels from a pool; pushes local → global
  position updates; supports reshape (box/sphere SDF). Use the provided
  helpers instead of iterating voxels individually: `deactivateAll()`,
  `activateAll()`, `changeVoxelColor(ivec3, Color)`, `changeVoxelColorAll(Color)`,
  `fillPlane(int axis, int planeIndex, Color)` (activates a single face slice),
  `reshape(Shape3D)` (box or sphere fill). All of these keep the pool's
  active-mask in sync. Custom carves/edits that these bulk mutators don't
  cover go through the encapsulated raw-edit API (#2165), never a hand-rolled
  `voxels_[i]` loop. `editVoxels(fn)` applies `fn(index, voxel, localPos)`
  to every voxel then resyncs once; `carve(shouldDeactivate)` is sugar over
  `editVoxels` for the common "deactivate voxels failing a predicate" case;
  `resyncAfterRawEdits()` is the escape hatch for a multi-pass edit that must
  still write the raw `voxels_` span directly across several loops — do all
  the writes, then call it once. Each entry point resyncs every derived
  invariant this set maintains (rotation-source mirror → pool active-mask →
  face occupancy) internally.

  Callers must **not** hand-roll `syncActiveMask()` +
  `IRPrefab::Voxel::recomputeFaceOccupancy(...)` themselves — dropping that
  pairing renders the carved set black under the lit/rotated path while the
  active-mask half looks done (the #2018/#2117/#2146 footgun the API exists
  to close). `syncActiveMask()` stays public as the low-level pool primitive
  for pre-existing raw-loop sites; prefer `editVoxels`/`carve` for new code.
  `simplify-check-ecs` flags a hand-rolled `voxels_[i].activate()/.deactivate()`
  carve loop followed by `syncActiveMask()`/`recomputeFaceOccupancy()` and
  steers toward the API.
- `C_ShapeDescriptor` — SDF shape type + params + color + flags (visible,
  hollow, mirror). Rendered directly by the GPU; **does not allocate voxels**.
- `C_Skeleton` — rig-root component holding an ordered vector of joint
  EntityIds. The position in the vector IS the bone_id stored in
  `C_Voxel.bone_id_`. At skinning time `UPDATE_JOINT_MATRICES` maps each
  bone_id to `slotBase + bone_id` in `EntityTransformBuffer` (binding 18)
  via per-voxel slots in `LocalVoxelPositions` (binding 17) — binding 21
  (`kBufferIndex_JointTransforms`) is SDF-shapes scaffolding only.
  See "Entity-based joints" below.
- `C_Joint` — tag marking an entity as a skeletal joint. Paired with the
  engine's canonical local-transform component and a `CHILD_OF` relation
  to the rig root (or to a parent joint).
- `C_JointHierarchy` — DEPRECATED; superseded by `C_Skeleton` + `C_Joint`.
  Remains for one release as a compile shim. See `component_joint_hierarchy.hpp`
  for the migration note.
- `C_BindPoints` — runtime mirror of an asset rig's BIND chunk
  (`IRAsset::Rig::bindPoints_`). Each entry stores
  `{boneId, offset, rotation}` keyed by name. Populated by
  `Prefab.spawn` from `rig_ref`; the per-name world transform is
  composed lazily by `IRPrefab::Rig::worldTransformForBindPoint` and
  surfaced to Lua as `IREntity.bindPoint(entity, "name")`. Per-name lookups use
  `unordered_map` and are documented as one-time queries at spawn or
  on interaction, not per-tick — see `engine/script/CLAUDE.md` for
  the Lua surface.

## Key systems

- `UPDATE_VOXEL_SET_CHILDREN` (UPDATE pipeline) — pushes per-voxel-set
  world-position updates into the pool, also registers ownership lookups.
  Translate-only path: voxels move with the entity's
  `C_WorldTransform.translation_` (composed by `PROPAGATE_TRANSFORM` from
  `C_LocalTransform` + parent chain + `TRANSFORM_TRANSLATION` modifiers)
  but no per-set rotation/scale composition. On-screen sets re-upload every
  frame (unconditional per ECS no-dirty-flags rule). Off-screen sets are
  skipped via `C_VoxelPool::isRangeVisible` against the previous frame's
  shadow-feeder-expanded cull viewport — same cull gate as `REBUILD_GRID_VOXELS`
  (#1288). A set entirely outside that viewport pays nothing; a visible set in
  a static scene re-uploads its positions every frame. Runs `PARALLEL_FOR`
  (#1803, `ParallelSafe`): disjoint per-set span writes, the canvas→pool
  lookup pre-resolved in `beginTick`, and the `queuePositionRange` hazard
  deferred into a per-worker accumulator merged in `endTick` — see
  `engine/system/CLAUDE.md` "Concurrency policy".
- `REBUILD_GRID_VOXELS` (UPDATE pipeline, T-294; inverse re-voxelize #1720)
  — Epic C C6. Runs AFTER `UPDATE_VOXEL_SET_CHILDREN`. Re-rasterizes
  GRID-mode entities (entities carrying `C_RotationMode::GRID`, the
  default) into rotated world cells from their live `C_WorldTransform`.
  Rotating sets render by **dest-lattice inverse resampling** (#1720, the
  CPU twin of the detached #1619 fix): walk the integer world cells of the
  rotated source AABB, inverse-map each via `roundHalfUp(R⁻¹·c)` into a
  per-set source occupancy grid, and author position + color + active per
  covered cell into the pool span — surjective, so no more forward-scatter
  coverage holes (which peaked at ~29% of a solid 12³ mid-rotation). The
  span contract is unchanged (`numVoxels_` slots): covered-cell overshoot
  is a small boundary fluctuation (≈ 2.8% observed for a solid 12³, ≤ 3.5%
  at 16³; zero for carved/thin shapes allocated as full boxes), and on
  overflow
  INTERIOR cells drop first, deterministically, so the visible surface
  always renders. While rotating, the authored colors live in
  `C_VoxelSetNew::rotationSourceVoxels_` (lazy snapshot; color mutators
  mirror into it; the identity frame restores the span and clears it) —
  raw `voxels_` span writes are only valid before a set's first rotated
  frame. On-screen sets re-rasterize every frame (no per-set
  transform-comparison early-out — that was a dirty flag in disguise, see
  `.claude/rules/cpp-ecs.md` "No dirty flags"); the only skip is a
  frustum-cull gate (`C_VoxelPool::isRangeVisible` against the
  shadow-feeder-expanded cull viewport), so sets whose pool chunks are all
  off-screen pay nothing. DETACHED-mode entities are skipped — they rotate
  through the per-canvas TRS composite (`ENTITY_CANVAS_TO_FRAMEBUFFER`)
  and never touch the world voxel pool's globals. Round-to-cell aliasing
  (which source voxel a contested dest cell shows) resolves first-wins in
  authored order — deterministic per pose. Each rotated cell it authors is
  stamped with `VoxelReserved::kRotatedEmit` (`reserved_` bit 2) so the
  voxel→trixel raster runs its silhouette-riser face selection on rotated
  staircases (emit the exposed opposite-polarity face the convex visible-triplet
  drops); non-rotated sets never carry the bit and keep the strict triplet. See
  `engine/render/CLAUDE.md` §"Voxel face rasterization". Math helpers:
  `IRPrefab::GridRotation::worldCellForGridVoxel` (forward; identity arm +
  creation-facing) and `sourceCellForWorldCell` (inverse) in
  `grid_rotation.hpp` — call directly from creations that need the same
  mapping outside the pipeline. Both helpers live outside the system header
  specifically so `test/ecs/grid_rotation_test.cpp` can exercise the math
  headlessly — when you add a public function here, extend that test file in
  the same change (the scaffolding already exists; a new function shipping
  untested is a review miss, #1732). Creations that spawn entities with
  `C_RotationMode::GRID` must register `REBUILD_GRID_VOXELS` in their
  UPDATE pipeline after `UPDATE_VOXEL_SET_CHILDREN`; omitting it produces
  silent no-ops.
- `REBUILD_DETACHED_VOXELS` (UPDATE pipeline, #1553 P1 / #1555, **repurposed P2
  / #1556**) — P1 re-rasterized a `RotationMode::DETACHED_REVOXELIZE` entity's
  **private** pool into full-rotation cell positions on the CPU every frame. P2
  moved that fill to the GPU compute (`c_revoxelize_detached`, dispatched
  from `VOXEL_TO_TRIXEL_STAGE_1` in place of `flushStaticPositionRanges`): the
  GPU now owns binding 5 on both backends, the only per-frame upload is the canvas
  quat (O(entities)), and this system's per-frame CPU rewrite is **gone**. The fill
  has two modes (`RevoxelizeDetachedParams.dest_.w`): at identity it forward-writes
  one cell per source voxel (byte-identical to #1556); while ROTATING it
  **inverse-resamples** (#1619) — one thread per DEST cell of the rotated-AABB cube
  inverse-maps `roundHalfUp(R⁻¹·c)` into a per-pool source occupancy+color grid
  (`C_DetachedRevoxelizeBuffer::sourceGrid_`) and authors position+color+active for
  hits. Forward scatter is not surjective onto the rotated lattice (covered dest
  cells got no source voxel → coverage holes / missing faces); inverse resampling
  is surjective → hole-free at every size. The shared compact/stage1/stage2 raster
  is untouched (slot `i` is now "dest cell i" not "source voxel i"). Its
  remaining job is to seed — ONCE — the conservative origin-centered world-AABB
  (`C_VoxelPool::setStaticReVoxelizeBound`, radius = farthest authored corner from
  the pool origin) that `rebuildChunkBounds` projects for the STAGE_1
  visibility gate, since the CPU global mirror no longer follows the rotation.
  Still ticks the CANVAS entity `(C_VoxelPool, C_CanvasLocalRotation)`, gated on
  `reVoxelize_`; early-outs once the bound is set (no per-frame work). Register
  AFTER `PROPAGATE_CANVAS_ROTATION`. The GPU compute mirrors
  `IRPrefab::GridRotation::worldCellForGridVoxel` (now `roundHalfUp`, the CPU↔GPU
  convention) about the pool ORIGIN, translation-free. **Invariant: one centered
  voxel set per private pool** — the conservative bound (and P1's per-pool single
  rotation) assume a centered solid; a future multi-set / off-origin detached
  canvas must rotate per set about each set's pivot and offset the bound.
  Detached pools are frustum-cull-exempt for the rotation (the conservative bound
  is rotation-independent), but a canvas-local pool still culls when the camera
  pans off the canvas origin — frame detached entities centered (#1555).
  Round-to-cell aliasing is accepted at P1/P2 (the spatial speckle was refined in
  epic P3 by the `.w=1` conservative dilation). **Temporal spin flicker is an
  accepted limitation** (#1570 D1): every frame re-voxelizes the rotated solid
  onto the integer cell grid via `roundHalfUp`, so the occupied-cell SET — and
  thus the emitted face set — flips discontinuously whenever a voxel crosses a
  half-integer boundary as the entity spins. This is the same round-to-cell
  source as the GRID stance above, manifesting temporally. It is **not** fixed
  with per-cell hysteresis / sticky cell keys: caching last-frame cell state to
  suppress flips is a snapshot-compare-and-early-return, which the no-dirty-flags
  rule forbids (`.claude/rules/cpp-ecs.md`). Supersampling the re-voxelize grid
  (scatter at Nx density, downsample) would trade flicker for fill cost + memory
  but is not justified at present — revisit only if it becomes a shipping blocker.
- `VOXEL_SQUASH_STRETCH` — animates voxel set scale/deformation via easing.
- A pool hierarchy/sort system exists but is commented out — **do not
  re-enable without a design pass.**
- A WIP scene/skeleton hierarchy traversal system is present but incomplete.

**Shadow-path invariant.** Any UPDATE-pipeline system that writes
`pool.getPositionGlobals()` (the source buffer for both
`VOXEL_TO_TRIXEL_STAGE_1` and the shadow bake) must gate on
`IRPrefab::SunShadow::shadowFeederCullViewport`, **not**
`cull.isoViewport(kCullChunkMargin)`. The shadow bake reads positions over
the sun-widened viewport; the narrower render viewport silently drops
off-screen casters' position updates, freezing their cast-shadow positions.
`REBUILD_GRID_VOXELS` and `UPDATE_VOXEL_SET_CHILDREN` both use
`shadowFeederCullViewport` for this reason — port the wider gate, not the
render gate, into any new position-writing system (#1764).

## Prefab.spawn voxel_ref → ECS components

**D2 restriction (Epic D #937):** SHAPES and HYBRID authoring are
deprecated for primary entities. Going forward, primary entity shapes
use DENSE `.vxs` assets; SHAPES (`C_ShapeDescriptor`) is reserved for
effects-only SDF entities (sun shadow occluders, auras, soft glows).
HYBRID authoring is fully deprecated — HYBRID `.vxs` files load for
backward-compat but no new assets should be authored in HYBRID mode.

`Prefab.spawn` (in `engine/script/`) routes a prefab's `voxel_ref`
through to runtime components when the loaded `.vxs` carries shape
records:

- **SHAPES shape half** (effects-only per D2) — one child entity per
  `IRAsset::ShapeRecord`, parented via `IREntity::setParent`. Each
  child gets `C_LocalTransform{record.offset_}` plus
  `C_ShapeDescriptor` populated from `record.shapeTypeId_` /
  `params_` / `color_` / `flags_`. `SYSTEM_PROPAGATE_TRANSFORM`
  composes the parent's `C_WorldTransform` with the child's
  `C_LocalTransform` so the rendered translation equals
  `parent.world + record.offset`. Per-record `rotation_`, `csgOp_`,
  and `boneId_` are loaded but not attached in v1 (no renderer
  consumes them; T-181 wires bone binding). Also fires for HYBRID
  assets during backward-compat load.
- **DENSE dense half** (primary entity path) — `C_VoxelSetNew`
  attached to the spawned root entity via
  `IRPrefab::DenseVoxel::toComponent` (`voxel/dense_bridge.hpp`).
  The bridge translates `IRAsset::DenseVoxelSet` → `C_VoxelSetNew`
  via a per-record copy (`VoxelRecord` and `C_Voxel` share the 12 B
  std430 layout but remain distinct types so layout drift surfaces as
  a compile error). Also fires for HYBRID during backward-compat
  load (both halves attach in a single spawn call).

`C_ShapeDescriptor`'s constructors snapshot the active canvas via
`IRRender::getActiveCanvasEntityOrNull()` rather than the
asserting `getActiveCanvasEntity()`. Headless construction
(prefab spawn in a test, asset tooling) gets
`canvasEntity_ = kNullEntity` instead of an `IR_ASSERT` failure;
iterating systems gate work on the field already.

## C_VoxelSetNew headless / staged mode

The dense-data ctor `C_VoxelSetNew(ivec3 boundsMin, ivec3 boundsMax,
span<const C_Voxel>)` is headless-safe: it snapshots the active
canvas via `getActiveCanvasEntityOrNull()` and branches:

- **Canvas active** — allocates from the pool, copies records into
  the span, seeds per-voxel positions at `boundsMin + indexToIvec3`.
  `numVoxels_` reflects the allocation; `pendingVoxels_` is empty.
- **Canvas absent (headless / pre-canvas)** — leaves `numVoxels_ = 0`
  and the pool spans empty, stages records in `pendingVoxels_`, and
  records the origin in `pendingBoundsMin_`. `canvasEntity_ ==
  kNullEntity` is the sentinel.

`recordCount()` returns the data count in either mode (test-friendly
and order-independent of pool availability). The **canvas-attach pass**
that moves `pendingVoxels_` into a pool allocation once a canvas
activates is `C_VoxelSetNew::attachToCanvas(canvas)` (persist P6 / W-10,
#2217): it allocates a span on the resolved canvas, seeds it from the
staged records (the same `seedIntoPool` body the dense ctor runs),
clears `pendingVoxels_`, and queues the range for GPU upload — a no-op
once the set is already pool-resident (`numVoxels_ > 0`). The
`SEED_STAGED_VOXELS` UPDATE system (`systems/system_seed_staged_voxels.hpp`)
drives it over every staged set each tick; register it before
`UPDATE_VOXEL_SET_CHILDREN` in a creation that loads a world snapshot so
freshly-deserialized sets attach and render on the first post-load frame.
Its no-op-once-seeded shape means the "is this set staged" gate is the
set's own honest state (`pendingVoxels_` non-empty), never a dirty flag.

`system_update_voxel_set_children` gates on `numVoxels_ > 0`, so a
staged headless set sitting in a canvas-active world contributes
nothing to the pool's per-frame writes — no risk of clobbering
adjacent voxel-set ranges. `onDestroy()` skips the pool
deallocation when `numVoxels_ == 0`, so the staging vector frees
with the component.

## Entity-based joints

Joint hierarchies are first-class entities, not vector entries inside a
single component on the rig root. Three pieces:

- **`C_Skeleton`** on the rig root. Holds `std::vector<EntityId> joints_`
  — the canonical, ordered list of joint entities. The index of an entry
  in `joints_` IS the `bone_id` stored in `C_Voxel.bone_id_`. At skinning
  time `UPDATE_JOINT_MATRICES` maps each bone_id to `slotBase + bone_id` in
  `EntityTransformBuffer` (binding `kBufferIndex_EntityTransforms`, slot 18)
  via per-voxel slots in `LocalVoxelPositions` (binding 17). Binding 21
  (`kBufferIndex_JointTransforms`) is SDF-shapes scaffolding only — not
  used by the voxel skinning path.
- **`C_Joint`** tag on each joint entity. Drives joint-only archetype
  queries like `<C_Joint, C_LocalTransform>` so IK solvers and the GPU
  joint-matrix uploader iterate joints without seeing rig roots or
  skinned voxel sets.
- **`CHILD_OF`** relations between joint entities form the bone tree. The
  same `PROPAGATE_TRANSFORM` (from `#731` Phase 2) that walks
  every other CHILD_OF hierarchy composes the parent chain — no
  joint-specific traversal code.

Each joint entity carries the engine's canonical local-transform component
(`C_LocalTransform`, since `#731` Phase 1 landed — PR #749). Joints can also
carry arbitrary gameplay components — IK targets,
constraints, hit-boxes, sound emitters, particle attachments — because the
joint is just an ECS entity. That is the whole point of the entity-based
model.

### Bone-index stability and severance

`C_Skeleton.joints_` is a flat ordered list — its indices are the bone-id
space. **Indices are stable across saves and severance.** Severing a joint
leaves a hole at the slot rather than splicing it out:

```
IRPrefab::Skeleton::severJoint(rigRoot, joint);
// 1. removeRelation(joint, rigRoot, CHILD_OF) — joint becomes orphan.
// 2. Walk descendants (still C_Joint, also orphaned).
// 3. For each voxel whose bone_id matches joint or its descendants,
//    bake the current world position into a new free-flying
//    C_VoxelSetNew on a new entity.
// 4. Mark the severed slot in C_Skeleton.joints_ as kNullEntity.
//    The matching GPU SSBO slot uploads as identity.
```

The severance API is **declared in this design, not yet implemented**.
It lives outside the current scaffolding ticket; the entity-based shape
exists in code first so consumer migration in `#605` Phase 2 has a stable
target. File a follow-up ticket when `#605` Phase 2 unblocks.

### Bind pose

Skinning math needs the bind-pose inverse to recover skinning matrices.
`C_Skeleton.bindPose_` (`std::vector<IRMath::SQT>`, parallel to `joints_` —
added in #605 Phase 2 / #1602) holds each joint's rest transform in
rig-root-local space. Populate it from a `.rig` via
`IRPrefab::Rig::bindPose(rig)` (`rig_bridge.hpp`), which folds the JNTS rest
chain with `IRMath::sqtCompose` — **not** the `.rig` BIND chunk, which stores
named attachment points (`C_BindPoints`), a separate concept despite the chunk
name. `IRPrefab::Skeleton::skinMatrix(jointWorld, bindPose_[i])`
(`skeleton.hpp`) returns `jointWorld × bindInverse`: identity at the bind pose,
the joint's posed deformation otherwise. The bind SQT is inverted analytically
(`IRMath::sqtInverse`) rather than via a 4×4 inverse. Under #605 Phase 2.3 the
per-joint skin matrix feeds the binding-18 transform buffer the
`c_update_voxel_positions` prepass already consumes.

### C_JointName

Optional tag on joint entities carrying the bone name string for editor
/ animation reference (`component_joint_name.hpp`). Shipped in #1607.
`C_Skeleton.joints_[i]` remains the authoritative index; bone names are
a UX convenience for editors and animation clips that address joints by
string.

## Gotchas

- **Carving `reserved_` bits? Update the layout comment in the same change.**
  When you repurpose sub-bits of a `reserved_` field in `C_Voxel` (or any
  GPU-mirrored std430 struct with a layout comment), update that struct's
  layout comment in the same commit. A stale "reserved for future fields"
  comment leads the next allocator to believe the bits are free and silently
  collide with the live encoding in the shader — no compile error, just
  corrupted GPU decode.
- **Never add `C_VoxelPool` to a non-canvas entity.** Pools are
  canvas-scoped. Only the canvas entity created by
  `IRRender::createCanvas` should own one.
- **`C_VoxelSetNew` allocates on construction.** The constructor goes
  through `IRPrefab::VoxelPool::allocate(...)` (see `voxel_pool_api.hpp`),
  which forwards into the render-side pool but keeps
  `component_voxel_set.hpp` free of `<irreden/ir_render.hpp>` — see the
  T-201 layering plan in `engine/script/CLAUDE.md`. If there's no active
  canvas the dense-data ctor stages to `pendingVoxels_`; the element-count
  ctor asserts (use the dense ctor for headless construction). Check
  `numVoxels_ > 0` after construction either way.
- **Position lag by one frame.** `C_WorldTransform.translation_` on a
  voxel set is only pushed to the pool by
  `system_update_voxel_set_children`. Any system that writes the
  entity's translation (or upstream modifier resolver +
  `PROPAGATE_TRANSFORM`) must run **before** that system in the
  pipeline or voxels lag a frame.
- **`onDestroy()` must run.** Destroying a voxel set without the
  destructor (e.g. by bypassing the entity manager) leaks its span.
  Stick to `IREntity::destroyEntity(id)`.
- **Shape descriptors vs voxel sets.** `C_ShapeDescriptor` is GPU-only
  (shaders evaluate the SDF directly) — it does *not* reserve voxels.
  `C_VoxelSetNew` pays memory but you can mutate individual cells.
  Choose the right tool for the use case.

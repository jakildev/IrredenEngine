# engine/prefabs/irreden/voxel/ — voxel pools, sets, shapes

Voxel rendering building blocks: pools, voxel sets (owned spans of a
pool), shape descriptors (SDF-based, GPU-resident), and entity builders
for single voxels and particles.

## Key components

- `C_Voxel` — per-voxel record (12 B std430): RGBA color + `material_id`,
  `flags` (bit-packed `VoxelFlags::kAoContrib | kEmissive | kInteractive`),
  `bone_id`, `layer_id` (editor layer membership; 0 = default layer).
  Default ctor sets `flags = kAoContrib` and the rest zero so v1 scenes
  render unchanged. Usually handled as spans inside a `C_VoxelPool`.
  Layout matches the GPU SSBO at slot 6 — see
  `components/component_voxel.hpp` and the per-pipeline shaders for the
  struct mirror.
- `C_VoxelPool` — master allocator; allocates/deallocates contiguous spans,
  tracks per-chunk bounds for visibility culling. **One pool per canvas entity.**
- `C_VoxelSetNew` — owns a span of voxels from a pool; pushes local → global
  position updates; supports reshape (box/sphere SDF). Use the provided
  helpers instead of iterating voxels individually: `deactivateAll()`,
  `activateAll()`, `changeVoxelColor(ivec3, Color)`, `changeVoxelColorAll(Color)`,
  `fillPlane(int axis, int planeIndex, Color)` (activates a single face slice),
  `reshape(Shape3D)` (box or sphere fill).
- `C_ShapeDescriptor` — SDF shape type + params + color + flags (visible,
  hollow, mirror). Rendered directly by the GPU; **does not allocate voxels**.
- `C_Skeleton` — rig-root component holding an ordered vector of joint
  EntityIds. The position in the vector IS the bone_id used by
  `C_Voxel.bone_id_` and indexed by the per-frame GPU joint-matrix SSBO.
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
  global-position updates into the pool, also registers ownership lookups.
  Translate-only path: voxels move with the entity's `C_PositionGlobal3D`
  but no rotation/scale composition.
- `REBUILD_GRID_VOXELS` (UPDATE pipeline, T-294) — Epic C C6. Runs AFTER
  `UPDATE_VOXEL_SET_CHILDREN`. Re-rasterizes GRID-mode entities (entities
  carrying `C_RotationMode::GRID`, the default) into rotated world cells
  whenever their `C_WorldTransform` changes. Cache lives on
  `C_VoxelSetNew::lastRebuild*_`; ticks are O(1) when the transform is
  unchanged. DETACHED-mode entities are skipped — they rotate through
  the per-canvas TRS composite (`ENTITY_CANVAS_TO_FRAMEBUFFER`) and
  never touch the world voxel pool's globals. Cell aliasing
  (multiple authored voxels collapsing into one world cell after
  rotation) is accepted by design; render-order is deterministic given
  stable entity ids. Math helper:
  `IRPrefab::GridRotation::worldCellForGridVoxel` in `grid_rotation.hpp`
  — call directly from creations that need the same mapping outside
  the pipeline.
- `VOXEL_SQUASH_STRETCH` — animates voxel set scale/deformation via easing.
- A pool hierarchy/sort system exists but is commented out — **do not
  re-enable without a design pass.**
- A WIP scene/skeleton hierarchy traversal system is present but incomplete.

## Prefab.spawn voxel_ref → ECS components

`Prefab.spawn` (in `engine/script/`) routes a prefab's `voxel_ref`
through to runtime components when the loaded `.vxs` carries shape
records:

- **SHAPES / HYBRID shape half** — one child entity per
  `IRAsset::ShapeRecord`, parented via `IREntity::setParent`. Each
  child gets `C_Position3D{record.offset_}` plus
  `C_ShapeDescriptor` populated from `record.shapeTypeId_` /
  `params_` / `color_` / `flags_`. CHILD_OF composition keeps the
  rendered position equal to `parent.global + record.offset`. Per-
  record `rotation_`, `csgOp_`, and `boneId_` are loaded but not
  attached in v1 (no renderer consumes them; T-181 wires bone
  binding).
- **DENSE / HYBRID dense half** — `C_VoxelSetNew` attached to the
  spawned root entity via `IRPrefab::DenseVoxel::toComponent`
  (`voxel/dense_bridge.hpp`). The bridge translates
  `IRAsset::DenseVoxelSet` → `C_VoxelSetNew` via a per-record copy
  (`VoxelRecord` and `C_Voxel` share the 12 B std430 layout but
  remain distinct types so layout drift surfaces as a compile
  error). For HYBRID, the SHAPES children and the DENSE
  `C_VoxelSetNew` land on the same root entity in a single
  `Prefab.spawn` call.

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
and order-independent of pool availability). A future task will add
the canvas-attach pass that moves `pendingVoxels_` into a pool
allocation once a canvas activates; until then the staged data
participates in serialization round-trips but does not render.

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
  in `joints_` IS the `bone_id` used by `C_Voxel.bone_id_` and indexed by
  the per-frame GPU joint-matrix SSBO at `kBufferIndex_JointTransforms`
  (defined in `engine/render/include/irreden/render/ir_render_types.hpp`).
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
`C_Skeleton` does **not** carry a `bindPose_` field yet — `IRMath::SQT` is
available since `#731` Phase 1 landed (PR #749); a follow-up (#605 Phase 2)
adds `std::vector<IRMath::SQT> bindPose_;` to `C_Skeleton` parallel to
`joints_`. Until then, callers that need a bind pose load it from the `.rig`
asset's BIND chunk via `IRPrefab::Rig::bindPose(rigRoot)`.

### Optional follow-up: C_JointName

The design also calls for an optional `C_JointName` tag carrying the
bone name string for editor / animation reference. Filed as a separate
follow-up rather than baked in here — `C_Skeleton.joints_[i]` is the
authoritative reference; bone names are a UX convenience for editors and
animation clips that want to address joints by string.

## Gotchas

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
- **Position lag by one frame.** `C_PositionGlobal3D` on a voxel set is
  only pushed to the pool by `system_update_voxel_set_children`. Any
  system that moves the entity must run **before** that system in the
  pipeline or voxels lag a frame.
- **`onDestroy()` must run.** Destroying a voxel set without the
  destructor (e.g. by bypassing the entity manager) leaks its span.
  Stick to `IREntity::destroyEntity(id)`.
- **Shape descriptors vs voxel sets.** `C_ShapeDescriptor` is GPU-only
  (shaders evaluate the SDF directly) — it does *not* reserve voxels.
  `C_VoxelSetNew` pays memory but you can mutate individual cells.
  Choose the right tool for the use case.

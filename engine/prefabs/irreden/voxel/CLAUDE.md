# engine/prefabs/irreden/voxel/ — voxel pools, sets, shapes

Voxel rendering building blocks: pools, voxel sets (owned spans of a
pool), shape descriptors (SDF-based, GPU-resident), and entity builders
for single voxels and particles.

## Key components

- `C_Voxel` — per-voxel record (12 B std430): RGBA color + `material_id`,
  `flags` (bit-packed `VoxelFlags::kAoContrib | kEmissive | kInteractive`),
  `bone_id`. Default ctor sets `flags = kAoContrib` and the rest zero so v1
  scenes render unchanged. Usually handled as spans inside a `C_VoxelPool`.
  Layout matches the GPU SSBO at slot 6 — see
  `components/component_voxel.hpp` and the per-pipeline shaders for the
  struct mirror.
- `C_VoxelPool` — master allocator; allocates/deallocates contiguous spans,
  tracks per-chunk bounds for visibility culling. **One pool per canvas entity.**
- `C_VoxelSetNew` — owns a span of voxels from a pool; pushes local → global
  position updates; supports reshape (box/sphere SDF).
- `C_ShapeDescriptor` — SDF shape type + params + color + flags (visible,
  hollow, mirror). Rendered directly by the GPU; **does not allocate voxels**.
- `C_JointHierarchy` — WIP; articulated voxel rigs.
- `C_BindPoints` — runtime mirror of an asset rig's BIND chunk
  (`IRAsset::Rig::bindPoints_`). Each entry stores
  `{boneId, offset, rotation}` keyed by name. Populated by
  `Prefab.spawn` from `rig_ref`; the per-name world transform is
  composed lazily by `IRPrefab::Rig::worldTransformForBindPoint` and
  surfaced to Lua as `entity:bindPoint("name")`. Per-name lookups use
  `unordered_map` and are documented as one-time queries at spawn or
  on interaction, not per-tick — see `engine/script/CLAUDE.md` for
  the Lua surface.

## Key systems

- `UPDATE_VOXEL_SET_CHILDREN` (UPDATE pipeline) — pushes per-voxel-set
  global-position updates into the pool, also registers ownership lookups.
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
- **DENSE / HYBRID dense half** — per-voxel records are loaded and
  validated but not attached. `C_VoxelSetNew` constructs against
  the active render-canvas pool; a headless attach path needs its
  own design. Track follow-up at the issue queue (deferred from
  T-182).

`C_ShapeDescriptor`'s constructors snapshot the active canvas via
`IRRender::getActiveCanvasEntityOrNull()` rather than the
asserting `getActiveCanvasEntity()`. Headless construction
(prefab spawn in a test, asset tooling) gets
`canvasEntity_ = kNullEntity` instead of an `IR_ASSERT` failure;
iterating systems gate work on the field already.

## Gotchas

- **Never add `C_VoxelPool` to a non-canvas entity.** Pools are
  canvas-scoped. Only the canvas entity created by
  `IRRender::createCanvas` should own one.
- **`C_VoxelSetNew` allocates on construction.** The constructor calls
  `IRRender::allocateVoxels(...)` against the active canvas pool. If
  there's no active canvas, allocation returns an empty span. Check
  `numVoxels_ > 0` after construction.
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

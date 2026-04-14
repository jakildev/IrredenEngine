# engine/prefabs/irreden/voxel/ — voxel pools, sets, shapes

Voxel rendering building blocks: pools, voxel sets (owned spans of a
pool), shape descriptors (SDF-based, GPU-resident), and entity builders
for single voxels and particles.

## Key components

- `C_Voxel` — a single RGBA color. Usually handled as spans inside a
  `C_VoxelPool`.
- `C_VoxelPool` — master allocator; allocates/deallocates contiguous spans,
  tracks per-chunk bounds for visibility culling. **One pool per canvas entity.**
- `C_VoxelSetNew` — owns a span of voxels from a pool; pushes local → global
  position updates; supports reshape (box/sphere SDF).
- `C_ShapeDescriptor` — SDF shape type + params + color + flags (visible,
  hollow, mirror). Rendered directly by the GPU; **does not allocate voxels**.
- `C_JointHierarchy` — WIP; articulated voxel rigs.

## Key systems

- `UPDATE_VOXEL_SET_CHILDREN` (UPDATE pipeline) — pushes per-voxel-set
  global-position updates into the pool, also registers ownership lookups.
- `VOXEL_SQUASH_STRETCH` — animates voxel set scale/deformation via easing.
- A pool hierarchy/sort system exists but is commented out — **do not
  re-enable without a design pass.**
- A WIP scene/skeleton hierarchy traversal system is present but incomplete.

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

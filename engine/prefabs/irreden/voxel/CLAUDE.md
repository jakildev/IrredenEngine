# engine/prefabs/irreden/voxel/ — voxel pools, sets, shapes

Voxel rendering building blocks: pools, voxel sets (owned spans of a
pool), shape descriptors (SDF-based, GPU-resident), and entity builders
for single voxels and particles.

## Key components

- `component_voxel.hpp` — `C_Voxel`, a single RGBA color. Usually
  handled as spans inside a `C_VoxelPool`.
- `component_voxel_pool.hpp` — `C_VoxelPool`. Master allocator;
  allocates/deallocates contiguous spans, tracks per-chunk bounds for
  visibility culling. **One pool per canvas entity.**
- `component_voxel_set.hpp` — `C_VoxelSetNew`. Owns a span of voxels
  from a pool; pushes local → global position updates; supports reshape
  (box/sphere SDF).
- `component_shape_descriptor.hpp` — `C_ShapeDescriptor`, SDF shape
  type + params + color + flags (visible, hollow, mirror). Rendered
  directly by the GPU; **does not allocate voxels**.
- `component_joint_hierarchy.hpp` — WIP; articulated voxel rigs.

## Key systems

- `system_update_voxel_set_children.hpp` — `UPDATE_VOXEL_SET_CHILDREN`,
  UPDATE pipeline. Pushes per-voxel-set global-position updates into
  the pool, also registers ownership lookups.
- `system_voxel_squash_stretch.hpp` — animates voxel set scale/
  deformation via easing.
- `system_voxel_pool.hpp` — commented out, was intended for pool
  hierarchy/sort. **Do not re-enable without a design pass.**
- `system_voxel_scene.hpp` — WIP skeleton/hierarchy traversal.

## Commands

- `command_randomize_voxels.hpp` — shuffle voxel colors in a set.
- `command_spawn_particle_mouse_position.hpp` — spawn a particle at
  the cursor's world position.

## Entity builders

- `entity_single_voxel.hpp` — `C_Position3D + C_Voxel`, for debug
  single-cell markers.
- `entity_voxel_particle.hpp` — 1×1×1 voxel set with `C_PeriodicIdle +
  C_Lifetime`. Used by the particle spawner.
- `entity_voxel_sprite.hpp` — commented out; was the image-to-voxel
  importer.

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

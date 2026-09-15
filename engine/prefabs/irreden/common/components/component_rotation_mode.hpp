#ifndef COMPONENT_ROTATION_MODE_H
#define COMPONENT_ROTATION_MODE_H

// Tags an entity with how its rotation composes against the world.
//
// - GRID (default): rotation discretizes to world-grid cells. Voxels
//   participate in the shared world voxel pool and a transform change
//   triggers SYSTEM_REBUILD_GRID_VOXELS to re-rasterize the authored
//   voxels into rotated world cells. Picks aliasing as a feature.
// - DETACHED: rotation lives inside a per-entity child canvas
//   (`C_EntityCanvas`) allocated at spawn time via
//   `IRPrefab::EntityCanvas::create()`. The world composite stage
//   (`system_entity_canvas_to_framebuffer`) threads the entity's
//   `C_LocalTransform` through the per-canvas TRS so the canvas
//   pitches/rolls/yaws as a unit without per-voxel rebake. Pair with
//   `C_LocalTransform::unbounded_ = true` to opt into sub-trixel
//   positioning (only meaningful when DETACHED — GRID quantizes
//   regardless).
// - DETACHED_REVOXELIZE: like DETACHED, but the private pool is re-filled
//   at the full-rotation cell positions each frame (SYSTEM_REBUILD_DETACHED_VOXELS)
//   and rasterized through cardinal frame data — the rotation lives in the
//   cells, not a 2D deform, so asymmetric solids read as true-3D.
//
// Entities without `C_RotationMode` are implicitly GRID — consumers
// default to GRID when the component is absent, so non-prefab entities
// are GRID.
// `IRPrefab::Prefab::spawnPrefab` always attaches the
// component so prefab-driven entities are discoverable by archetype
// queries.
//
// The re-rasterize path honors that default through a second query arm:
// `SYSTEM_REBUILD_GRID_VOXELS_IMPLICIT` runs the identical GRID
// body over `Exclude<C_RotationMode>`, so a creation that registers
// `REBUILD_GRID_VOXELS` must register the implicit twin next to it — omit
// it and a component-less entity's authored rotation renders as identity
// with nothing logged. See `engine/prefabs/irreden/voxel/CLAUDE.md`
// §"Transform and revoxelization pipeline" for the pair-registration rule.
//
// Mode is mutable at runtime via `IRPrefab::RotationMode::setMode`
// (in `engine/prefabs/irreden/common/rotation_mode.hpp`) at a
// re-allocation cost — switching to a canvas-owning mode allocates a new
// entity canvas; switching to GRID destroys it. DETACHED and
// DETACHED_REVOXELIZE are one family for this purpose (the predicate is
// `IRPrefab::RotationMode::ownsEntityCanvas`), so swapping between them
// keeps the canvas rather than churning it. Don't mutate `mode_`
// directly; the helper keeps `C_EntityCanvas` in sync.

#include <cstdint>

namespace IRComponents {

// kFirst / kLast bracket the valid range so binding-layer range checks
// (e.g. the `rotation_mode` schema validator in
// `engine/script/src/prefab_api.cpp`) stay automatic when a new mode
// is added — bumping kLast in lockstep keeps the validator honest
// without each call site re-hard-coding the latest sentinel. Pattern
// is documented in `.claude/rules/cpp-lua-enums.md`.
enum class RotationMode : std::uint8_t {
    GRID = 0,
    DETACHED = 1,
    DETACHED_REVOXELIZE = 2,

    kFirst = GRID,
    kLast = DETACHED_REVOXELIZE,
};

struct C_RotationMode {
    RotationMode mode_ = RotationMode::GRID;

    C_RotationMode() = default;
    explicit C_RotationMode(RotationMode mode)
        : mode_{mode} {}
};

} // namespace IRComponents

#endif /* COMPONENT_ROTATION_MODE_H */

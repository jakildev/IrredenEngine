#ifndef COMPONENT_ROTATION_MODE_H
#define COMPONENT_ROTATION_MODE_H

// Tags an entity with how its rotation composes against the world.
//
// - GRID (default): rotation discretizes to world-grid cells. Voxels
//   participate in the shared world voxel pool and a transform change
//   triggers SYSTEM_REBUILD_GRID_VOXELS (C6) to re-rasterize the
//   authored voxels into rotated world cells. Picks aliasing as a
//   feature.
// - DETACHED: rotation lives inside a per-entity child canvas
//   (`C_EntityCanvas`) allocated at spawn time via
//   `IRPrefab::EntityCanvas::create()`. The world composite stage
//   (`system_entity_canvas_to_framebuffer`) threads the entity's
//   `C_LocalTransform` through the per-canvas TRS so the canvas
//   pitches/rolls/yaws as a unit without per-voxel rebake. Pair with
//   `C_LocalTransform::unbounded_ = true` to opt into sub-trixel
//   positioning (only meaningful when DETACHED — GRID quantizes
//   regardless).
//
// Entities without `C_RotationMode` are implicitly GRID — consumers
// default to GRID when the component is absent so non-prefab entities
// (test scaffolding, ad-hoc createEntity callers) keep today's
// behavior. `IRPrefab::Prefab::spawnPrefab` always attaches the
// component so prefab-driven entities are discoverable by archetype
// queries.
//
// Mode is mutable at runtime via `IRPrefab::RotationMode::setMode`
// (in `engine/prefabs/irreden/common/rotation_mode.hpp`) at a
// re-allocation cost — switching to DETACHED allocates a new entity
// canvas; switching to GRID destroys it. Don't mutate `mode_`
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
    // Attached main-canvas SO(3) rotation is the GRID re-voxelize model
    // (`SYSTEM_REBUILD_GRID_VOXELS`), where the camera alone drives trixel
    // deformation and the entity's rotation only changes which cells are
    // filled. The retired `MAIN_CANVAS_SO3` mode (#1272 / #1299) — a GPU
    // position-transform + octahedral-snap + per-entity visible triplet on the
    // shared canvas — was removed in #1443: a tilted-axis face deformation
    // can't be represented under the main canvas's fixed-(1,1,1) iso-depth-axis
    // invariant, so per-entity trixel deformation lives on DETACHED canvases.

    kFirst = GRID,
    kLast = DETACHED,
};

struct C_RotationMode {
    RotationMode mode_ = RotationMode::GRID;

    C_RotationMode() = default;
    explicit C_RotationMode(RotationMode mode)
        : mode_{mode} {}
};

} // namespace IRComponents

#endif /* COMPONENT_ROTATION_MODE_H */

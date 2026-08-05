#ifndef IR_PREFAB_ROTATION_MODE_H
#define IR_PREFAB_ROTATION_MODE_H

// Prefab-scoped helpers for `C_RotationMode`. The component itself is
// plain data; cross-entity orchestration — allocating the per-entity
// canvas on the detached modes, destroying it on GRID — lives here so
// the component layout stays trivial and archetype-iteration friendly.
//
// Spawn-time mode selection is handled by `IRPrefab::Prefab::spawnPrefab`
// directly. Use `setMode` to change an already-spawned entity's mode at
// runtime; it preserves the rest of the entity's components and pays
// the re-allocation cost (one canvas-entity create or destroy) inline.
//
// Both lifecycle sites read `ownsEntityCanvas` rather than each spelling
// out which modes own a canvas.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/entity_canvas.hpp>

#include <string>

namespace IRPrefab::RotationMode {

/// True when `mode` keeps the entity's rotation on a per-entity
/// `C_EntityCanvas`. DETACHED and DETACHED_REVOXELIZE differ in how that
/// canvas is *filled* — a 2D forward-scatter deform versus a per-frame
/// re-voxelize at full-rotation cell positions — not in whether one
/// exists. Both allocate at spawn and both must release at teardown.
///
/// Canvas lifecycle is decided in exactly two places — `setMode` and
/// `IRPrefab::Prefab::spawnPrefab` — and both call this predicate rather
/// than spelling their own mode list, so a new mode cannot be wired into
/// one site and missed by the other. Adding a mode means classifying it
/// here; `test/ecs/rotation_mode_set_test.cpp` static-asserts the enum's
/// size so that stays mandatory rather than remembered. See #2908.
inline constexpr bool ownsEntityCanvas(IRComponents::RotationMode mode) {
    return mode == IRComponents::RotationMode::DETACHED ||
           mode == IRComponents::RotationMode::DETACHED_REVOXELIZE;
}

/// Transition an entity to `newMode`, allocating or destroying its
/// per-entity canvas as required.
///
/// - GRID → a canvas-owning mode: allocates a child canvas via
///   `IRPrefab::EntityCanvas::create(canvasName, canvasSize)` and
///   attaches `C_EntityCanvas` to `entity`.
/// - A canvas-owning mode → GRID: destroys the entity's `C_EntityCanvas`
///   child entity (freeing its GPU textures via `onDestroy`) and removes
///   the component from `entity`.
/// - DETACHED ↔ DETACHED_REVOXELIZE: keeps the existing canvas. Both
///   modes own one, so the swap is a re-tag, not a re-allocation.
/// - Same mode in/out: a no-op **when the canvas already matches the
///   mode**. When it does not, the call reconciles it.
///
/// The mismatch case is load-bearing, not defensive. `spawnPrefab`
/// deliberately tags an entity into a canvas-owning mode *without*
/// allocating when it runs with no `RenderManager` (headless tooling,
/// unit tests) and documents this call as the recovery once one exists.
/// Gating the early return on the mode alone would make that recovery a
/// no-op and strand the entity canvas-less.
///
/// `canvasName` and `canvasSize` are only consulted when a canvas is
/// actually allocated; pass sensible defaults otherwise.
inline void setMode(
    IREntity::EntityId entity,
    IRComponents::RotationMode newMode,
    std::string canvasName = {},
    IRMath::ivec2 canvasSize = IRMath::ivec2{0}
) {
    using IRComponents::C_EntityCanvas;
    using IRComponents::C_RotationMode;

    auto modeOpt = IREntity::getComponentOptional<C_RotationMode>(entity);
    const IRComponents::RotationMode current =
        modeOpt ? modeOpt.value()->mode_ : IRComponents::RotationMode::GRID;

    auto canvasOpt = IREntity::getComponentOptional<C_EntityCanvas>(entity);
    const bool hasCanvas = canvasOpt.has_value();
    const bool wantsCanvas = ownsEntityCanvas(newMode);

    if (current == newMode && hasCanvas == wantsCanvas) {
        return;
    }

    // Release only when leaving the canvas-owning family entirely — a
    // DETACHED ↔ DETACHED_REVOXELIZE swap keeps the canvas it already has.
    // Read the child id out before destroying anything; the component
    // pointer is not held across the structural change.
    if (hasCanvas && !wantsCanvas) {
        const IREntity::EntityId canvas = canvasOpt.value()->canvasEntity_;
        if (canvas != IREntity::kNullEntity) {
            IREntity::destroyEntity(canvas);
        }
        IREntity::removeComponent<C_EntityCanvas>(entity);
    }

    if (wantsCanvas && !hasCanvas) {
        IR_ASSERT(
            IRRender::g_renderManager != nullptr,
            "setMode() into a canvas-owning rotation mode requires a live RenderManager"
        );
        IREntity::setComponent(entity, IRPrefab::EntityCanvas::create(canvasName, canvasSize));
    }

    IREntity::setComponent(entity, C_RotationMode{newMode});
}

} // namespace IRPrefab::RotationMode

#endif /* IR_PREFAB_ROTATION_MODE_H */

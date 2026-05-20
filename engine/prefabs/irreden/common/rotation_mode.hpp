#ifndef IR_PREFAB_ROTATION_MODE_H
#define IR_PREFAB_ROTATION_MODE_H

// Prefab-scoped helpers for `C_RotationMode`. The component itself is
// plain data; cross-entity orchestration — allocating the per-entity
// canvas on DETACHED, destroying it on GRID — lives here so the
// component layout stays trivial and archetype-iteration friendly.
//
// Spawn-time mode selection is handled by `IRPrefab::Prefab::spawnPrefab`
// directly. Use `setMode` to change an already-spawned entity's mode at
// runtime; it preserves the rest of the entity's components and pays
// the re-allocation cost (one canvas-entity create or destroy) inline.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/entity_canvas.hpp>

#include <string>

namespace IRPrefab::RotationMode {

/// Transition an entity to `newMode`, allocating or destroying its
/// per-entity canvas as required.
///
/// - GRID → DETACHED: allocates a child canvas via
///   `IRPrefab::EntityCanvas::create(canvasName, canvasSize)` and
///   attaches `C_EntityCanvas` to `entity`. The previous canvas (if
///   any) is left alone — re-entering DETACHED from DETACHED is a
///   no-op rather than a re-allocation churn.
/// - DETACHED → GRID: destroys the entity's `C_EntityCanvas` child
///   entity (freeing its GPU textures via `onDestroy`) and removes
///   the component from `entity`.
/// - Same mode in/out: no-op.
///
/// `canvasName` and `canvasSize` are only consulted on a GRID→DETACHED
/// transition; pass sensible defaults otherwise.
inline void setMode(
    IREntity::EntityId entity,
    IRComponents::RotationMode newMode,
    std::string canvasName = {},
    IRMath::ivec2 canvasSize = IRMath::ivec2{0}
) {
    using IRComponents::C_EntityCanvas;
    using IRComponents::C_RotationMode;
    using IRComponents::RotationMode;

    auto modeOpt = IREntity::getComponentOptional<C_RotationMode>(entity);
    const RotationMode current = modeOpt ? modeOpt.value()->mode_ : RotationMode::GRID;
    if (current == newMode) {
        return;
    }

    if (newMode == RotationMode::DETACHED) {
        auto existing = IREntity::getComponentOptional<C_EntityCanvas>(entity);
        if (!existing) {
            IREntity::setComponent(
                entity, IRPrefab::EntityCanvas::create(canvasName, canvasSize)
            );
        }
    } else { // GRID
        auto existing = IREntity::getComponentOptional<C_EntityCanvas>(entity);
        if (existing) {
            const IREntity::EntityId canvas = existing.value()->canvasEntity_;
            if (canvas != IREntity::kNullEntity) {
                IREntity::destroyEntity(canvas);
            }
            IREntity::removeComponent<C_EntityCanvas>(entity);
        }
    }

    IREntity::setComponent(entity, C_RotationMode{newMode});
}

} // namespace IRPrefab::RotationMode

#endif /* IR_PREFAB_ROTATION_MODE_H */

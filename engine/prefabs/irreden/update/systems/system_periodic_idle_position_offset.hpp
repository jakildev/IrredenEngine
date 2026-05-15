#ifndef SYSTEM_PERIODIC_IDLE_POSITION_OFFSET_H
#define SYSTEM_PERIODIC_IDLE_POSITION_OFFSET_H

// Re-pushes the entity's idle-bob value as a vec3 ADD modifier each
// UPDATE tick. APPLY_POSITION_OFFSET later composes the entity's
// vec3 modifiers and adds the resolved offset to C_PositionGlobal3D.
//
// `ticksRemaining_ = 1` because this writer pushes AFTER MODIFIER_DECAY
// has already run in the same UPDATE pipeline: the entry survives this
// frame's compose, then next frame's DECAY decrements it (1 → 0) and
// removes it before the next compose. The "use 2 to fire for one frame"
// rule in MODIFIER_DECAY's header applies to entries pushed OUTSIDE the
// pipeline (Lua, input handlers) — those see DECAY before their first
// compose, so they need one extra tick of headroom.
//
// Pipeline ordering required by callers:
//   PERIODIC_IDLE → MODIFIER_DECAY → PERIODIC_IDLE_POSITION_OFFSET
//   → ... → GLOBAL_POSITION_3D → APPLY_POSITION_OFFSET
// MODIFIER_DECAY must run before this system or the per-entity vector
// grows without bound.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/position_modifier_fields.hpp>
#include <irreden/update/components/component_periodic_idle.hpp>

namespace IRSystem {

template <> struct System<PERIODIC_IDLE_POSITION_OFFSET> {
    static SystemId create() {
        return createSystem<IRComponents::C_PeriodicIdle, IRComponents::C_Modifiers>(
            "PeriodicIdlePositionOffset",
            [](IREntity::EntityId entity,
               IRComponents::C_PeriodicIdle &idle,
               IRComponents::C_Modifiers &mods) {
                // Push directly to the archetype-iterated mods to avoid
                // a per-entity getComponent inside IRPrefab::Modifier::push.
                mods.modifiersVec3_.push_back(
                    IRComponents::ModifierVec3{
                        IRPrefab::PositionModifier::positionOffsetField(),
                        IRComponents::TransformKind::ADD,
                        idle.getValue(),
                        entity,
                        1
                    }
                );
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_PERIODIC_IDLE_POSITION_OFFSET_H */

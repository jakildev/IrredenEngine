#ifndef SYSTEM_PERIODIC_IDLE_POSITION_OFFSET_H
#define SYSTEM_PERIODIC_IDLE_POSITION_OFFSET_H

// Re-pushes the entity's idle-bob value as a vec3 ADD modifier each
// UPDATE tick. APPLY_POSITION_OFFSET later composes the entity's
// vec3 modifiers and adds the resolved offset to C_PositionGlobal3D.
//
// `ticksRemaining_ = 2` is the smallest value that fires for exactly
// one frame (decay decrements at the start of the next frame, removing
// the entry before the next compose). The bob writer re-pushes every
// tick, so steady-state holds at most two modifiers per entity.
//
// Pipeline ordering required by callers:
//   PERIODIC_IDLE → MODIFIER_DECAY → PERIODIC_IDLE_POSITION_OFFSET
//   → ... → GLOBAL_POSITION_3D → APPLY_POSITION_OFFSET
// MODIFIER_DECAY must run before this system or the per-entity vector
// grows without bound.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/modifier.hpp>
#include <irreden/common/position_modifier_fields.hpp>
#include <irreden/update/components/component_periodic_idle.hpp>

namespace IRSystem {

template <> struct System<PERIODIC_IDLE_POSITION_OFFSET> {
    static SystemId create() {
        return createSystem<IRComponents::C_PeriodicIdle, IRComponents::C_Modifiers>(
            "PeriodicIdlePositionOffset",
            [](IREntity::EntityId entity,
               IRComponents::C_PeriodicIdle &idle,
               [[maybe_unused]] IRComponents::C_Modifiers &) {
                IRPrefab::Modifier::push(
                    entity,
                    IRPrefab::PositionModifier::positionOffsetField(),
                    IRComponents::TransformKind::ADD,
                    idle.getValue(),
                    entity,
                    2
                );
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_PERIODIC_IDLE_POSITION_OFFSET_H */

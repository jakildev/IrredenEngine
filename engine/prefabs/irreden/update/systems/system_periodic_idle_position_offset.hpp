#ifndef SYSTEM_PERIODIC_IDLE_POSITION_OFFSET_H
#define SYSTEM_PERIODIC_IDLE_POSITION_OFFSET_H

// Re-pushes the entity's idle-bob value as a vec3 ADD modifier each
// UPDATE tick. APPLY_POSITION_OFFSET later composes the entity's
// vec3 modifiers and adds the resolved offset to C_PositionGlobal3D.
//
// Uses pushFrameLocal (ticksRemaining=1) because this writer runs AFTER
// MODIFIER_DECAY in the same UPDATE pipeline: the entry survives this
// frame's compose, then next frame's DECAY removes it.
//
// Pipeline ordering required by callers:
//   PERIODIC_IDLE → MODIFIER_DECAY → PERIODIC_IDLE_POSITION_OFFSET
//   → ... → GLOBAL_POSITION_3D → APPLY_POSITION_OFFSET
// MODIFIER_DECAY must run before this system or the per-entity vector
// grows without bound.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>

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
                IRPrefab::Modifier::pushFrameLocal(
                    mods,
                    IRPrefab::PositionModifier::positionOffsetField(),
                    IRComponents::TransformKind::ADD,
                    idle.getValue(),
                    entity
                );
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_PERIODIC_IDLE_POSITION_OFFSET_H */

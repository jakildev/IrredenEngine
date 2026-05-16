#ifndef SYSTEM_PERIODIC_IDLE_POSITION_OFFSET_H
#define SYSTEM_PERIODIC_IDLE_POSITION_OFFSET_H

// Upserts the entity's idle-bob value as a vec3 ADD modifier each UPDATE
// tick via upsertBySourceInPlace. The slot key is (entity, POSITION_OFFSET_3D,
// ADD) — one entry per bob-eligible entity, updated in place each tick.
// No ticksRemaining_ countdown; no MODIFIER_DECAY dependency.
// APPLY_POSITION_OFFSET later composes the vec3 modifiers and adds the
// resolved offset to C_PositionGlobal3D.
//
// Pipeline ordering required by callers:
//   PERIODIC_IDLE → PERIODIC_IDLE_POSITION_OFFSET
//   → ... → GLOBAL_POSITION_3D → APPLY_POSITION_OFFSET

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
                IRPrefab::Modifier::upsertBySourceInPlace(
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

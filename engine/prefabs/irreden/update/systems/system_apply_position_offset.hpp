#ifndef SYSTEM_APPLY_POSITION_OFFSET_H
#define SYSTEM_APPLY_POSITION_OFFSET_H

// Composes the entity's vec3 modifiers for the POSITION_OFFSET_3D
// channel inline against a vec3(0) base and adds the result to
// C_PositionGlobal3D. Inline compose (no C_ResolvedFields, no resolver
// tick) keeps the cost flat for creations that only need the offset
// channel — entity opt-in is C_Modifiers.
//
// Pipeline placement: AFTER GLOBAL_POSITION_3D so globalPos = local +
// parent has been refreshed for this frame, and AFTER the per-frame
// writer that pushes the offset modifier.

#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/common/modifier_compose.hpp>
#include <irreden/common/position_modifier_fields.hpp>

namespace IRSystem {

template <> struct System<APPLY_POSITION_OFFSET> {
    static SystemId create() {
        return createSystem<IRComponents::C_PositionGlobal3D, IRComponents::C_Modifiers>(
            "ApplyPositionOffset",
            [](IRComponents::C_PositionGlobal3D &globalPos, IRComponents::C_Modifiers &mods) {
                globalPos.pos_ += IRPrefab::Modifier::detail::composeForFieldVec3(
                    IRMath::vec3(0.0f),
                    IRPrefab::PositionModifier::positionOffsetField(),
                    mods.modifiersVec3_
                );
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_APPLY_POSITION_OFFSET_H */

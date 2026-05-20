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
//
// Single-line instantiation of `applyVec3ModifierTo` — see
// `engine/prefabs/irreden/common/modifier_apply.hpp` for the family
// shape and inline-apply vs. structured-resolver guidance.

#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/common/modifier_apply.hpp>
#include <irreden/common/position_modifier_fields.hpp>

namespace IRSystem {

template <> struct System<APPLY_POSITION_OFFSET> {
    static SystemId create() {
        return IRPrefab::Modifier::applyVec3ModifierTo<
            IRComponents::C_PositionGlobal3D,
            &IRComponents::C_PositionGlobal3D::pos_>(
            "ApplyPositionOffset",
            IRPrefab::PositionModifier::positionOffsetField()
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_APPLY_POSITION_OFFSET_H */

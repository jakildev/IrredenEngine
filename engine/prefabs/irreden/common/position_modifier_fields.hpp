#ifndef POSITION_MODIFIER_FIELDS_H
#define POSITION_MODIFIER_FIELDS_H

// Well-known vec3 modifier field for the per-entity "added on top of
// global position" channel. Writers push a vec3 modifier (idle bob,
// gizmo nudges, future per-frame perturbations); APPLY_POSITION_OFFSET
// composes the entity's vec3 modifiers and adds the resolved value to
// C_PositionGlobal3D once per UPDATE tick.
//
// Lazy-registered on first call so the engine doesn't pay the registry
// slot in creations that don't use the channel.

#include <irreden/common/modifier.hpp>

namespace IRPrefab::PositionModifier {

inline IRComponents::FieldBindingId positionOffsetField() {
    static const IRComponents::FieldBindingId id =
        IRPrefab::Modifier::registerFieldVec3("POSITION_OFFSET_3D");
    return id;
}

} // namespace IRPrefab::PositionModifier

#endif /* POSITION_MODIFIER_FIELDS_H */

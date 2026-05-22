#ifndef POSITION_MODIFIER_FIELDS_H
#define POSITION_MODIFIER_FIELDS_H

// Legacy POSITION_OFFSET_3D modifier-field accessor. Retained for callers
// still pushing modifiers on the legacy channel during the SQT consumer
// migration (T-199 / T-300+); new code should push under
// IRPrefab::TransformModifier::translationField() so SYSTEM_PROPAGATE_TRANSFORM
// folds the offset directly into C_WorldTransform.translation_. This
// header (and the field) retire once T-302 drops the legacy components.
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

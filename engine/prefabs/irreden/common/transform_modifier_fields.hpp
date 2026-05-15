#ifndef TRANSFORM_MODIFIER_FIELDS_H
#define TRANSFORM_MODIFIER_FIELDS_H

// Well-known vec3 modifier fields read by SYSTEM_PROPAGATE_TRANSFORM
// each tick. Writers (per-frame perturbations: shake, recoil, wobble,
// animation-blend overlays) push vec3 modifiers under these field ids;
// the modifier resolver composes them into C_ResolvedFields; the
// propagation system reads the resolved values and folds them into the
// world transform per the SQT formula in
// `engine/prefabs/irreden/common/CLAUDE.md`.
//
// Compose semantics: TRANSFORM_TRANSLATION accumulates additively
// (base vec3(0), ADD/MULTIPLY/SET per-axis); TRANSFORM_SCALE composes
// multiplicatively against a base of vec3(1). Both are vec3 fields —
// scalar pushes silently no-op per the modifier framework rule.
//
// The matching ROTATION quat field is registered by T-198 once the
// quat modifier kind ships; until then, the propagation system reads
// modifier rotation as identity.
//
// Lazy-registered on first call so the engine does not pay the
// registry slot in creations that do not use these channels.

#include <irreden/common/modifier.hpp>

namespace IRPrefab::TransformModifier {

inline IRComponents::FieldBindingId translationField() {
    static const IRComponents::FieldBindingId id =
        IRPrefab::Modifier::registerFieldVec3("TRANSFORM_TRANSLATION");
    return id;
}

inline IRComponents::FieldBindingId scaleField() {
    static const IRComponents::FieldBindingId id =
        IRPrefab::Modifier::registerFieldVec3("TRANSFORM_SCALE");
    return id;
}

} // namespace IRPrefab::TransformModifier

#endif /* TRANSFORM_MODIFIER_FIELDS_H */

#ifndef COMPONENT_WORLD_TRANSFORM_H
#define COMPONENT_WORLD_TRANSFORM_H

// World-space SQT, computed by SYSTEM_PROPAGATE_TRANSFORM from the
// entity's C_LocalTransform composed with its parent chain's
// C_WorldTransform. Same layout/identity as C_LocalTransform (vec4 quat
// with .w as scalar).
//
// Consumers (render, gizmo, physics) read this and never C_LocalTransform
// for draw position — the local is per-parent and only the propagation
// system resolves the chain.

#include <irreden/ir_math.hpp>

#include <irreden/entity/ir_entity_types.hpp>

namespace IRComponents {

struct C_WorldTransform {
    IRMath::vec3 scale_ = IRMath::vec3(1.0f, 1.0f, 1.0f);
    IRMath::vec4 rotation_ = IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    IRMath::vec3 translation_ = IRMath::vec3(0.0f, 0.0f, 0.0f);

    C_WorldTransform() = default;

    C_WorldTransform(IRMath::vec3 translation, IRMath::vec4 rotation, IRMath::vec3 scale)
        : scale_{scale}
        , rotation_{rotation}
        , translation_{translation} {}
};

} // namespace IRComponents

namespace IREntity {

// Derived pose of a static resident (docs/design/static-residents.md):
// only a deriving system may declare a write to it on one.
template <>
inline constexpr StaticGuard kStaticGuard<IRComponents::C_WorldTransform> = StaticGuard::DERIVED;

} // namespace IREntity

#endif /* COMPONENT_WORLD_TRANSFORM_H */

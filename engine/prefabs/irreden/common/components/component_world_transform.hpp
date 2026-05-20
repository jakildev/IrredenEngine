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

#endif /* COMPONENT_WORLD_TRANSFORM_H */

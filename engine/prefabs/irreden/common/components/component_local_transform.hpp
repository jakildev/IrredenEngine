#ifndef COMPONENT_LOCAL_TRANSFORM_H
#define COMPONENT_LOCAL_TRANSFORM_H

// Canonical local-space SQT (scale, quaternion rotation, translation).
// Reads as "the entity's transform relative to its parent in the
// CHILD_OF graph". Roots use their local transform unchanged as world.
//
// Quaternion layout matches IRMath / IRAsset::RigJoint: vec4(qx, qy, qz, qw)
// with .w as the scalar — identity is vec4(0, 0, 0, 1). Compose via
// IRMath::quatMul (Hamilton, parent_world * local in column-vector
// convention) and IRMath::rotateVectorByQuat. See engine/math/CLAUDE.md
// "Quaternions" for the algebra contract.
//
// Pair with C_WorldTransform; SYSTEM_PROPAGATE_TRANSFORM walks the
// parent chain in topological order, composes parent.world * local
// (with modifier-resolved per-frame perturbations folded in), and
// writes C_WorldTransform.

#include <irreden/ir_math.hpp>

namespace IRComponents {

struct C_LocalTransform {
    IRMath::vec3 scale_ = IRMath::vec3(1.0f, 1.0f, 1.0f);
    IRMath::vec4 rotation_ = IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    IRMath::vec3 translation_ = IRMath::vec3(0.0f, 0.0f, 0.0f);

    C_LocalTransform() = default;

    C_LocalTransform(IRMath::vec3 translation)
        : translation_{translation} {}

    C_LocalTransform(IRMath::vec3 translation, IRMath::vec4 rotation)
        : rotation_{rotation}
        , translation_{translation} {}

    C_LocalTransform(IRMath::vec3 translation, IRMath::vec4 rotation, IRMath::vec3 scale)
        : scale_{scale}
        , rotation_{rotation}
        , translation_{translation} {}
};

} // namespace IRComponents

#endif /* COMPONENT_LOCAL_TRANSFORM_H */

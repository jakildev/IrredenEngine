#ifndef GIZMO_DRAG_MATH_H
#define GIZMO_DRAG_MATH_H

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/ir_math.hpp>

namespace IRPrefab::Gizmo {

inline IRMath::vec3
translationAxisWorld(IRMath::vec3 localAxis, const IRComponents::C_WorldTransform &anchorWorld) {
    const IRMath::vec3 direction =
        IRMath::rotateVectorByQuat(anchorWorld.scale_ * localAxis, anchorWorld.rotation_);
    const float length = IRMath::length(direction);
    return length > 0.0f ? direction / length : IRMath::vec3(0.0f);
}

// Cursor deltas live on a fixed world plane, while the anchor's translation
// lives in its parent's coordinates. The visible arrow chooses the world axis.
inline IRMath::vec3 translationDeltaInParent(
    IRMath::vec3 cursorWorldDelta,
    IRMath::vec3 axisWorld,
    const IRComponents::C_WorldTransform &parentWorld
) {
    if (parentWorld.scale_.x == 0.0f || parentWorld.scale_.y == 0.0f ||
        parentWorld.scale_.z == 0.0f) {
        return IRMath::vec3(0.0f);
    }
    const IRMath::vec3 deltaWorld = axisWorld * IRMath::dot(cursorWorldDelta, axisWorld);
    return IRMath::rotateVectorByQuat(deltaWorld, IRMath::quatInverse(parentWorld.rotation_)) /
           parentWorld.scale_;
}

} // namespace IRPrefab::Gizmo

#endif /* GIZMO_DRAG_MATH_H */

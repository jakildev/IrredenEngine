#ifndef SYSTEM_ROTATION_TARGET_LOCAL_TRANSFORM_H
#define SYSTEM_ROTATION_TARGET_LOCAL_TRANSFORM_H

// SYSTEM_ROTATION_TARGET_LOCAL_TRANSFORM — maps each entity's C_RotationTarget
// normalized input onto an angle and writes it as an absolute rotation about
// the target axis into C_LocalTransform.rotation_. The rotation sibling of
// SYSTEM_GOTO_3D (which writes eased local translation).
//
// Register in UPDATE before PROPAGATE_TRANSFORM so the new local rotation
// propagates to C_WorldTransform in the same tick (and, for GRID-mode entities,
// drives REBUILD_GRID_VOXELS' re-rasterization downstream) — same ordering
// contract as AUTO_SPIN_LOCAL_TRANSFORM.
//
// Writes only the iterating entity's own C_LocalTransform and reads only its own
// (const) C_RotationTarget — same shape as AUTO_SPIN_LOCAL_TRANSFORM, so
// PARALLEL_FOR is safe. kEasingFunctions holds stateless lambdas, safe to call
// from multiple threads at once.

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/update/components/component_rotation_target.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<ROTATION_TARGET_LOCAL_TRANSFORM> {
    static constexpr Concurrency kConcurrency = Concurrency::PARALLEL_FOR;

    void tick(C_LocalTransform &localXform, const C_RotationTarget &target) {
        // quatAxisAngle normalizes the axis internally, so a zero axis would
        // produce a NaN quaternion that PROPAGATE_TRANSFORM leaks into
        // C_WorldTransform. Skip and leave the existing rotation untouched.
        if (IRMath::dot(target.axis_, target.axis_) == 0.0f) {
            return;
        }
        const float range = target.inputMax_ - target.inputMin_;
        const float t = range != 0.0f
            ? IRMath::clamp((target.input_ - target.inputMin_) / range, 0.0f, 1.0f)
            : 0.0f;
        const float angle =
            target.minAngle_ +
            (target.maxAngle_ - target.minAngle_) * target.easingFunction_(t);
        localXform.rotation_ = IRMath::quatAxisAngle(target.axis_, angle);
    }

    static SystemId create() {
        return registerSystem<ROTATION_TARGET_LOCAL_TRANSFORM, C_LocalTransform, C_RotationTarget>(
            "RotationTargetLocalTransform"
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_ROTATION_TARGET_LOCAL_TRANSFORM_H */

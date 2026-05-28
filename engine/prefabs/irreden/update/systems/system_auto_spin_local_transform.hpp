#ifndef SYSTEM_AUTO_SPIN_LOCAL_TRANSFORM_H
#define SYSTEM_AUTO_SPIN_LOCAL_TRANSFORM_H

// SYSTEM_AUTO_SPIN_LOCAL_TRANSFORM — per-entity continuous rotation around
// `C_AutoSpin::axis_` at `radiansPerFrame_` rate. Composed on the LEFT of
// the current local rotation so the spin accumulates in the local frame,
// matching the column-vector / parent_world * local Hamilton convention
// used by PROPAGATE_TRANSFORM (see engine/math/CLAUDE.md "Quaternions").
//
// Register in UPDATE before PROPAGATE_TRANSFORM so the new local rotation
// propagates to C_WorldTransform in the same tick (and, for GRID-mode
// entities, drives REBUILD_GRID_VOXELS' re-rasterization downstream).
//
// Writes only the iterating entity's own C_LocalTransform and reads only
// its own C_AutoSpin — same shape as VELOCITY_3D, so PARALLEL_FOR is safe.

#include <irreden/common/components/component_auto_spin.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<AUTO_SPIN_LOCAL_TRANSFORM> {
    static constexpr Concurrency kConcurrency = Concurrency::PARALLEL_FOR;

    void tick(C_LocalTransform &localXform, const C_AutoSpin &spin) {
        if (spin.radiansPerFrame_ == 0.0f) {
            return;
        }
        const vec4 delta = IRMath::quatAxisAngle(spin.axis_, spin.radiansPerFrame_);
        localXform.rotation_ = IRMath::quatMul(delta, localXform.rotation_);
    }

    static SystemId create() {
        return registerSystem<AUTO_SPIN_LOCAL_TRANSFORM, C_LocalTransform, C_AutoSpin>(
            "AutoSpinLocalTransform"
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_AUTO_SPIN_LOCAL_TRANSFORM_H */

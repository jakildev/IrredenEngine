#ifndef SYSTEM_ANIM_MOTION_COLOR_SHIFT_H
#define SYSTEM_ANIM_MOTION_COLOR_SHIFT_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/update/components/component_action_animation.hpp>
#include <irreden/update/components/component_anim_color_state.hpp>
#include <irreden/update/components/component_anim_motion_color_shift.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<ANIMATION_MOTION_COLOR_SHIFT> {
    static SystemId create() {
        return createSystem<
            C_ActionAnimation,
            C_AnimColorState,
            C_AnimMotionColorShift,
            C_VoxelSetNew>(
            "AnimMotionColorShift",
            [](const C_ActionAnimation &anim,
               C_AnimColorState &colorState,
               C_AnimMotionColorShift &shift,
               C_VoxelSetNew &voxelSet) {
                if (voxelSet.numVoxels_ <= 0) return;

                float dt = IRTime::deltaTime(IRTime::UPDATE);

                if (anim.isPlaying()) {
                    shift.currentBlend_ += shift.fadeInSpeed_ * dt;
                } else {
                    shift.currentBlend_ -= shift.fadeOutSpeed_ * dt;
                }
                shift.currentBlend_ = IRMath::clamp(shift.currentBlend_, 0.0f, 1.0f);

                if (shift.currentBlend_ <= 0.0f) return;

                Color result = IRMath::lerpColor(
                    colorState.currentColor_, shift.motionColor_, shift.currentBlend_);

                colorState.currentColor_ = result;
                voxelSet.changeVoxelColorAll(result);
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_ANIM_MOTION_COLOR_SHIFT_H */

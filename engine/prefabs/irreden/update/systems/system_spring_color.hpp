#ifndef SYSTEM_SPRING_COLOR_H
#define SYSTEM_SPRING_COLOR_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/update/components/component_spring_platform.hpp>
#include <irreden/update/components/component_anim_color_state.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<SPRING_COLOR> {
    static SystemId create() {
        return createSystem<
            C_SpringPlatform,
            C_AnimColorState,
            C_VoxelSetNew>(
            "SpringColor",
            [](const C_SpringPlatform &spring,
               C_AnimColorState &colorState,
               C_VoxelSetNew &voxelSet) {
                if (voxelSet.numVoxels_ <= 0) return;

                if (!colorState.baseInitialized_) {
                    colorState.baseColor_ = voxelSet.voxels_[0].color_;
                    colorState.currentColor_ = colorState.baseColor_;
                    colorState.baseInitialized_ = true;
                }

                // colorProgress: 0 = at-rest end, 1 = fully locked end.
                // IDLE/REBOUNDING hold releaseColorShift (progress=0).
                // LOCKED/ANTICIPATING hold lockColorShift (progress=1).
                // CATCHING/LAUNCHING blend between release and lock.
                ColorHSV offset;
                switch (spring.state_) {
                case SPRING_LOCKED:
                case SPRING_ANTICIPATING:
                    offset = spring.lockColorShift_;
                    break;
                case SPRING_CATCHING:
                case SPRING_LAUNCHING:
                    offset = IRMath::lerpHSV(
                        spring.releaseColorShift_,
                        spring.lockColorShift_,
                        spring.colorProgress_);
                    break;
                case SPRING_REBOUNDING:
                case SPRING_IDLE:
                default:
                    offset = spring.releaseColorShift_;
                    break;
                }
                Color shifted = IRMath::applyHSVOffset(
                    colorState.baseColor_, offset);
                if (spring.colorMinValue_ > 0.0f
                    || spring.colorMinSaturation_ > 0.0f) {
                    ColorHSV hsv = IRMath::colorToColorHSV(shifted);
                    hsv.value_ = std::max(hsv.value_, spring.colorMinValue_);
                    hsv.saturation_ = std::max(
                        hsv.saturation_, spring.colorMinSaturation_);
                    shifted = IRMath::colorHSVToColor(hsv);
                }
                colorState.currentColor_ = shifted;
                voxelSet.changeVoxelColorAll(colorState.currentColor_);
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_SPRING_COLOR_H */

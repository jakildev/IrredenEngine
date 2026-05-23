#ifndef SYSTEM_GOTO_3D_H
#define SYSTEM_GOTO_3D_H

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/update/components/component_goto_easing_3d.hpp>
#include <irreden/ir_math.hpp>

using namespace IRComponents;

namespace IRSystem {

template <> struct System<GOTO_3D> {
    static SystemId create() {
        return createSystem<C_LocalTransform, C_GotoEasing3D>(
            "Goto3D",
            [](C_LocalTransform &localXform, C_GotoEasing3D &gotoComp) {
                if (gotoComp.done_)
                    return;
                gotoComp.currentFrame_++;
                localXform.translation_ = IRMath::mix(
                    gotoComp.startPos_,
                    gotoComp.endPos_,
                    gotoComp.easingFunction_(
                        static_cast<float>(gotoComp.currentFrame_) /
                        static_cast<float>(gotoComp.durationFrames_)
                    )
                );
                if (gotoComp.currentFrame_ >= gotoComp.durationFrames_) {
                    gotoComp.done_ = true;
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_GOTO_3D_H */

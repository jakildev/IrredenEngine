#ifndef SYSTEM_GOTO_3D_H
#define SYSTEM_GOTO_3D_H

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/update/components/component_goto_easing_3d.hpp>
#include <irreden/ir_math.hpp>

using namespace IRComponents;

namespace IRSystem {

template <> struct System<GOTO_3D> {
    static SystemId create() {
        // static std::vector<EntityId> m_finishedEntities;
        return createSystem<C_Position3D, C_GotoEasing3D>(
            "Goto3D",
            [](C_Position3D &position, C_GotoEasing3D &gotoComp) {
                if (gotoComp.done_)
                    return;
                gotoComp.currentFrame_++;
                position.pos_ = IRMath::mix(
                    gotoComp.startPos_.pos_,
                    gotoComp.endPos_.pos_,
                    gotoComp.easingFunction_(
                        static_cast<float>(gotoComp.currentFrame_) /
                        static_cast<float>(gotoComp.durationFrames_)
                    )
                );
                if (gotoComp.currentFrame_ >= gotoComp.durationFrames_) {
                    gotoComp.done_ = true;
                    // m_finishedEntities.push_back(entities[i]);
                }
            },
            []() {
                // m_finishedEntities.clear();
            },
            []() {
                // for(auto& entity : m_finishedEntities) {
                //     IRECS::removeComponent<C_GotoEasing3D>(entity);
                // }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_GOTO_3D_H */

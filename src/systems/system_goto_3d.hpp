/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\systems\system_goto_3d.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_GOTO_3D_H
#define SYSTEM_GOTO_3D_H

#include "..\ecs\ir_system_base.hpp"

#include "..\components\component_position_3d.hpp"
#include "..\components\component_goto_easing_3d.hpp"

using namespace IRComponents;

namespace IRECS {

    template<>
    class IRSystem<GOTO_3D> : public IRSystemBase<
        GOTO_3D,
        C_Position3D,
        C_GotoEasing3D
    >   {
    public:
        IRSystem() {
            ENG_LOG_INFO("Created system GOTO_3D");
        }
        virtual ~IRSystem() = default;

        void tickWithArchetype(
            Archetype archetype,
            std::vector<EntityId>& entities,
            std::vector<C_Position3D>& positions,
            std::vector<C_GotoEasing3D>& gotos
        )
        {
            for(int i=0; i < entities.size(); i++) {
                auto& gotoComp = gotos[i];
                if(gotoComp.done_) continue;
                gotoComp.currentFrame_++;
                positions[i].pos_ = glm::mix(
                    gotoComp.startPos_.pos_,
                    gotoComp.endPos_.pos_,
                    gotoComp.easingFunction_(
                        static_cast<float>(gotoComp.currentFrame_) /
                        static_cast<float>(gotoComp.durationFrames_)
                    )
                );
                // maybe just have a way to remove components
                // right here because that would be a lot more intuitive
                // iterate over local copy if it is non const and copy back at end
                if(gotoComp.currentFrame_ >= gotoComp.durationFrames_) {
                    gotoComp.done_ = true;
                    // m_finishedEntities.push_back(entities[i]);
                }
            }
        }

    private:
        std::vector<EntityId> m_finishedEntities;

        virtual void beginExecute() override {
            m_finishedEntities.clear();
        }
        virtual void endExecute() override {
            // TODO: Remove components batched from same archetype node
            // This should be a feature of base class or something
            for(auto& entity : m_finishedEntities) {
                EntityHandle handle{entity};
                handle.remove<C_GotoEasing3D>();
            }
        }
    };

} // namespace IRECS

#endif /* SYSTEM_GOTO_3D_H */

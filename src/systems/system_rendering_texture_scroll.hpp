/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\systems\system_rendering_texture_scroll.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_RENDERING_TEXTURE_SCROLL_H
#define SYSTEM_RENDERING_TEXTURE_SCROLL_H

#include "..\ecs\ir_system_base.hpp"

#include "..\components\component_texture_scroll.hpp"
#include "..\time\time_manager.hpp"

using namespace IRComponents;
using namespace IRMath;

namespace IRECS {

    template<>
    class IRSystem<RENDERING_TEXTURE_SCROLL> : public IRSystemBase<
        RENDERING_TEXTURE_SCROLL,
        C_TextureScrollPosition,
        C_TextureScrollVelocity
    >   {
    public:
        IRSystem()
        {
            ENG_LOG_INFO("Created system system-name-here");
        }

        void tickWithArchetype(
            Archetype type,
            std::vector<EntityId>& entities,
            std::vector<C_TextureScrollPosition>& textureScrollPositions,
            const std::vector<C_TextureScrollVelocity>& textureScrollVelocities
        )
        {
            for(int i=0; i < entities.size(); i++) {
                vec2 textureStepThisFrame = vec2(
                    textureScrollVelocities[i].velocity_.x *
                        global.timeManager_->deltaTime<IRTime::RENDER>(),
                    textureScrollVelocities[i].velocity_.y *
                        global.timeManager_->deltaTime<IRTime::RENDER>()
                );
                textureScrollPositions[i].position_
                    += textureStepThisFrame;
            }
        }
    private:
        // virtual void beginExecute() override {
        //     EASY_FUNCTION(IR_PROFILER_COLOR_RENDER);
        // }

        // virtual void endExecute() override {
        //     EASY_FUNCTION(IR_PROFILER_COLOR_RENDER);
        // }



    };


} // namespace IRSystem

#endif /* SYSTEM_RENDERING_TEXTURE_SCROLL_H */

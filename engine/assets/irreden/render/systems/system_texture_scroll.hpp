/*
 * Project: Irreden Engine
 * File: system_rendering_texture_scroll.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_RENDERING_TEXTURE_SCROLL_H
#define SYSTEM_RENDERING_TEXTURE_SCROLL_H

#include <irreden/ir_ecs.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/render/components/component_texture_scroll.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRECS {

    template<>
    class System<RENDERING_TEXTURE_SCROLL> : public SystemBase<
        RENDERING_TEXTURE_SCROLL,
        C_TextureScrollPosition,
        C_TextureScrollVelocity
    >   {
    public:
        System()
        {
            IRProfile::engLogInfo("Created system RENDERING_TEXTURE_SCROLL");
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
                        IRTime::deltaTime(IRTime::Events::RENDER),
                    textureScrollVelocities[i].velocity_.y *
                        IRTime::deltaTime(IRTime::Events::RENDER)
                );
                textureScrollPositions[i].position_
                    += textureStepThisFrame;
            }
        }
    private:

    };


} // namespace IRECS

#endif /* SYSTEM_RENDERING_TEXTURE_SCROLL_H */

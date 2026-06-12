#ifndef SYSTEM_TEXTURE_SCROLL_H
#define SYSTEM_TEXTURE_SCROLL_H

#include <irreden/ir_ecs.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/render/components/component_texture_scroll.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<TEXTURE_SCROLL> {
    static constexpr Concurrency kConcurrency = Concurrency::PARALLEL_FOR;

    void tick(
        C_TextureScrollPosition &textureScrollPosition,
        const C_TextureScrollVelocity &textureScrollVelocity
    ) {
        vec2 textureStepThisFrame = vec2(
            textureScrollVelocity.velocity_.x *
                // IDEA: deltaTime should know what event based
                // on some higher level pipeline thing
                IRTime::deltaTime(IRTime::Events::RENDER),
            textureScrollVelocity.velocity_.y * IRTime::deltaTime(IRTime::Events::RENDER)
        );
        textureScrollPosition.position_ += textureStepThisFrame;
    }

    static SystemId create() {
        return registerSystem<TEXTURE_SCROLL, C_TextureScrollPosition, C_TextureScrollVelocity>(
            "TextureScroll"
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_TEXTURE_SCROLL_H */

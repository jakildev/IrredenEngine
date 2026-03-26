#ifndef COMPONENT_PROCEDURAL_ANIMATION_H
#define COMPONENT_PROCEDURAL_ANIMATION_H

#include <irreden/ir_math.hpp>
#include <irreden/render/ir_render_types.hpp>

using namespace IRMath;

namespace IRComponents {

struct C_ProceduralAnimation {
    float time_ = 0.0f;
    float speed_ = 1.0f;
    float phase_ = 0.0f;
    vec4 blend_ = vec4(1.0f, 0.0f, 0.0f, 0.0f);

    C_ProceduralAnimation() = default;

    C_ProceduralAnimation(float speed, float phase = 0.0f)
        : speed_{speed}
        , phase_{phase} {}

    void advance(float deltaTime) {
        time_ += deltaTime * speed_;
    }

    IRRender::GPUAnimationParams toGPUFormat() const {
        IRRender::GPUAnimationParams p{};
        p.time = time_;
        p.speed = speed_;
        p.phase = phase_;
        p.blend = blend_;
        return p;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_PROCEDURAL_ANIMATION_H */

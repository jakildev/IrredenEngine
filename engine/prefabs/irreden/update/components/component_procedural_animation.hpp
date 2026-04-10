#ifndef COMPONENT_PROCEDURAL_ANIMATION_H
#define COMPONENT_PROCEDURAL_ANIMATION_H

// PURPOSE: Per-entity procedural animation parameters (time, speed, phase,
//   blend weights) for GPU-driven animation. Converts to GPUAnimationParams
//   for upload to the AnimBuffer SSBO (binding 22) consumed by the shapes
//   compute shader.
// STATUS: WIP stub -- component defined with toGPUFormat() but:
//   - No system advances time_ each frame.
//   - No SystemName enum entry (PROCEDURAL_ANIMATION) exists.
//   - system_shapes_to_trixel.hpp creates the AnimationParamsBuffer SSBO
//     but never uploads data; c_shapes_to_trixel.glsl declares AnimBuffer
//     but main() never reads from it.
// TODO:
//   1. Add PROCEDURAL_ANIMATION to SystemName enum.
//   2. Create system_procedural_animation.hpp that advances time_ using
//      IRTime::deltaTime(IRTime::UPDATE) each tick.
//   3. Extend SHAPES_TO_TRIXEL (or create a new system) to upload
//      GPUAnimationParams per entity to the AnimationParamsBuffer.
//   4. Update c_shapes_to_trixel.glsl main() to read animation params
//      and apply them (e.g. modulate shape params or position over time).
//   5. Create a demo entity to validate end-to-end.
// DEPENDENCIES: IRRender (GPUAnimationParams), IRMath (vec4).

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

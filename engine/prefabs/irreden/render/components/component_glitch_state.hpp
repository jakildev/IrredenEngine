#ifndef COMPONENT_GLITCH_STATE_H
#define COMPONENT_GLITCH_STATE_H

#include <cstdint>

namespace IRComponents {

// World-scoped singleton driving every glitch pass. Plain data: validation,
// the tick clock and the Lua surface live in `IRPrefab::Glitch`
// (`glitch.hpp`), which is the only writer that checks the invariants below.
//
// Invariants a stored value satisfies: every float finite; `intensity_` and
// the six weights in [0, 1]; `periodSeconds_ > 0`; `holdDivisor_ >= 1`.
struct C_GlitchState {
    // 0 is off. Scales band counts, displacement and bar brightness together.
    float intensity_ = 0.0f;
    // Every random choice re-rolls once per `periodSeconds_`, on ticks offset
    // by `phaseSeconds_` on the engine's fixed-step clock.
    double periodSeconds_ = 1.0;
    double phaseSeconds_ = 0.0;
    std::uint32_t seed_ = 0;
    float tearWeight_ = 0.0f;
    float holdWeight_ = 0.0f;
    float splitWeight_ = 0.0f;
    float barsWeight_ = 0.0f;
    float cellsWeight_ = 0.0f;
    float depthWeight_ = 0.0f;
    // The hold history re-samples every `periodSeconds_ / holdDivisor_`.
    int holdDivisor_ = 1;
};

} // namespace IRComponents

#endif /* COMPONENT_GLITCH_STATE_H */

// Easing function implementations come from the GLM library

#ifndef EASING_FUNCTIONS_H
#define EASING_FUNCTIONS_H

#include <glm/gtx/easing.hpp>
#include <unordered_map>
#include <functional>

namespace IRMath {

/// Identifies an easing curve backed by `glm/gtx/easing.hpp`.
/// Not every GLM easing overload is exposed — some variants (e.g. back-ease
/// overshoot parameters) are pre-configured with engine defaults in
/// @ref kEasingFunctions.
enum IREasingFunctions {
    kLinearInterpolation,
    kQuadraticEaseIn,
    kQuadraticEaseOut,
    kQuadraticEaseInOut,
    kCubicEaseIn,
    kCubicEaseOut,
    kCubicEaseInOut,
    kQuarticEaseIn,
    kQuarticEaseOut,
    kQuarticEaseInOut,
    kQuinticEaseIn,
    kQuinticEaseOut,
    kQuinticEaseInOut,
    kSineEaseIn,
    kSineEaseOut,
    kSineEaseInOut,
    kCircularEaseIn,
    kCircularEaseOut,
    kCircularEaseInOut,
    kExponentialEaseIn,
    kExponentialEaseOut,
    kExponentialEaseInOut,
    kElasticEaseIn,
    kElasticEaseOut,
    kElasticEaseInOut,
    kBackEaseIn,
    kBackEaseOut,
    kBackEaseInOut,
    kBounceEaseIn,
    kBounceEaseOut,
    kBounceEaseInOut
};

/// Callable type for a single-argument easing function: `float f(float t)`
/// where `t ∈ [0, 1]` and the return value is typically in [0, 1] (some
/// curves like elastic and back overshoot this range).
using GLMEasingFunction = std::function<float(const float &)>;

/// Dispatch table mapping each @ref IREasingFunctions variant to its
/// GLM-backed implementation.  Back-ease variants use engine-default
/// overshoot factors rather than raw GLM defaults.
const std::unordered_map<IREasingFunctions, GLMEasingFunction> kEasingFunctions = {
    {kLinearInterpolation, glm::linearInterpolation<float>},
    {kQuadraticEaseIn, glm::quadraticEaseIn<float>},
    {kQuadraticEaseOut, glm::quadraticEaseOut<float>},
    {kQuadraticEaseInOut, glm::quadraticEaseInOut<float>},
    {kCubicEaseIn, glm::cubicEaseIn<float>},
    {kCubicEaseOut, glm::cubicEaseOut<float>},
    {kCubicEaseInOut, glm::cubicEaseInOut<float>},
    {kQuarticEaseIn, glm::quarticEaseIn<float>},
    {kQuarticEaseOut, glm::quarticEaseOut<float>},
    {kQuarticEaseInOut, glm::quarticEaseInOut<float>},
    {kQuinticEaseIn, glm::quinticEaseIn<float>},
    {kQuinticEaseOut, glm::quinticEaseOut<float>},
    {kQuinticEaseInOut, glm::quinticEaseInOut<float>},
    {kSineEaseIn, glm::sineEaseIn<float>},
    {kSineEaseOut, glm::sineEaseOut<float>},
    {kSineEaseInOut, glm::sineEaseInOut<float>},
    {kCircularEaseIn, glm::circularEaseIn<float>},
    {kCircularEaseOut, glm::circularEaseOut<float>},
    {kCircularEaseInOut, glm::circularEaseInOut<float>},
    {kExponentialEaseIn, glm::exponentialEaseIn<float>},
    {kExponentialEaseOut, glm::exponentialEaseOut<float>},
    // GLM 1.1.0 exponentialEaseInOut lacks boundary guards: at t=0 the formula
    // evaluates to 0.5*2^(-10) ≈ 1/2048 instead of 0, and at t=1 it evaluates
    // to 1-1/2048 instead of 1.  Wrap with explicit boundary clamps so the
    // function satisfies f(0)=0 and f(1)=1 as expected by all callers.
    {kExponentialEaseInOut, [](float t) -> float {
        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;
        return glm::exponentialEaseInOut(t);
    }},
    {kElasticEaseIn, glm::elasticEaseIn<float>},
    {kElasticEaseOut, glm::elasticEaseOut<float>},
    {kElasticEaseInOut, glm::elasticEaseInOut<float>},
    {kBackEaseIn, [](float t) { return glm::backEaseIn(t, 0.0f); }},
    {kBackEaseOut, [](float t) { return glm::backEaseOut(t, 6.0f); }},
    {kBackEaseInOut, [](float t) { return glm::backEaseInOut(t, 0.5f); }},
    {kBounceEaseIn, glm::bounceEaseIn<float>},
    {kBounceEaseOut, glm::bounceEaseOut<float>},
    {kBounceEaseInOut, glm::bounceEaseInOut<float>}
};
} // namespace IRMath

#endif /* EASING_FUNCTIONS_H */

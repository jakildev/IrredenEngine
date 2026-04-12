// Easing function implementations come from glm/gtx/easing.hpp

#ifndef EASING_FUNCTIONS_H
#define EASING_FUNCTIONS_H

#include <glm/gtx/easing.hpp>
#include <unordered_map>
#include <functional>

namespace IRMath {

/// Enumeration of all available easing curves.
/// Use as a key into `kEasingFunctions` to retrieve a callable that maps a
/// normalized time `t ∈ [0, 1]` to an eased output value.
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

/// Callable type for a GLM easing function: maps normalized `t ∈ [0, 1]` to
/// an eased output. Stored as `std::function` so lambdas (e.g. back-ease with
/// a captured overshoot parameter) can be held alongside raw function pointers.
using GLMEasingFunction = std::function<float(const float &)>;

/// Maps every `IREasingFunctions` variant to its GLM implementation.
/// Look up an easing function by key and call it with a normalized `t` value.
///
/// Note: the three back-ease variants use non-default overshoot parameters:
/// `backEaseIn(t, 0.0)` — no overshoot on the approach;
/// `backEaseOut(t, 6.0)` — exaggerated overshoot on the release;
/// `backEaseInOut(t, 0.5)` — mild overshoot in both directions.
/// These were tuned for the engine's animation feel; change with care.
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
    {kExponentialEaseInOut, glm::exponentialEaseInOut<float>},
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

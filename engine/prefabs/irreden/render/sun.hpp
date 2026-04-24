#ifndef IR_PREFAB_SUN_H
#define IR_PREFAB_SUN_H

// Driver-side API for the directional sun vector consumed by the
// COMPUTE_SUN_SHADOW pass. Singleton state — one sun direction
// globally, stored in an `inline` variable so every translation unit
// that includes this header sees the same value.
//
// `setDirection` normalizes on write so callers can pass any non-zero
// vector; a zero-length input is clamped to the overhead-lit default.

#include <irreden/ir_math.hpp>

using namespace IRMath;

namespace IRPrefab::Sun {

namespace detail {
// Unit vector pointing from surfaces toward the sun. Default is
// overhead-lit; creations override via setDirection() per frame or at
// init. Consumed by the COMPUTE_SUN_SHADOW pass each frame.
inline vec3 g_direction = vec3(0.0f, 1.0f, 0.0f);
} // namespace detail

inline void setDirection(vec3 dir) {
    const float len = glm::length(dir);
    detail::g_direction = len > 0.0f ? dir / len : vec3(0.0f, 1.0f, 0.0f);
}

inline vec3 getDirection() {
    return detail::g_direction;
}

} // namespace IRPrefab::Sun

#endif /* IR_PREFAB_SUN_H */

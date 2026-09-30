#ifndef COMPONENT_HITBOX_2D_H
#define COMPONENT_HITBOX_2D_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>

using namespace IRMath;

namespace IRComponents {

struct C_HitBox2D {
    vec2 halfExtent_;
    float padding_ = IRConstants::kDefaultPickPadding;
    bool hovered_ = false;
    bool enabled_ = true;
    bool screenSpaceCenter_ = false;
    vec2 centerScreen_{};
    int pickPriority_ = 0;
    int isoDepth_ = 0;

    C_HitBox2D()
        : halfExtent_{0.0f, 0.0f} {}

    C_HitBox2D(vec2 halfExtent)
        : halfExtent_{halfExtent} {}

    C_HitBox2D(float width, float height)
        : halfExtent_{width * 0.5f, height * 0.5f} {}
};

} // namespace IRComponents

#endif /* COMPONENT_HITBOX_2D_H */

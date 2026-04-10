#ifndef COMPONENT_HITBOX_2D_H
#define COMPONENT_HITBOX_2D_H

#include <irreden/ir_math.hpp>

using namespace IRMath;

namespace IRComponents {

struct C_HitBox2D {
    vec2 halfExtent_;
    bool hovered_ = false;

    C_HitBox2D()
        : halfExtent_{0.0f, 0.0f} {}

    C_HitBox2D(vec2 halfExtent)
        : halfExtent_{halfExtent} {}

    C_HitBox2D(float width, float height)
        : halfExtent_{width * 0.5f, height * 0.5f} {}
};

} // namespace IRComponents

#endif /* COMPONENT_HITBOX_2D_H */

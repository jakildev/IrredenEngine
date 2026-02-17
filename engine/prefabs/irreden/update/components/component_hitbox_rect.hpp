#ifndef COMPONENT_HITBOX_RECT_H
#define COMPONENT_HITBOX_RECT_H

#include <irreden/ir_math.hpp>

using IRMath::u8vec2;

namespace IRComponents {

struct C_HitboxRect {
    u8vec2 size_;

    C_HitboxRect(u8vec2 size) : size_{size} {}

    // Default
    C_HitboxRect() : size_{u8vec2{1, 1}} {}
};

} // namespace IRComponents

#endif /* COMPONENT_HITBOX_RECT_H */

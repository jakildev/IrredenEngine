#ifndef COMPONENT_TEXTURE_SCROLL_H
#define COMPONENT_TEXTURE_SCROLL_H

#include <irreden/ir_math.hpp>

using IRMath::vec2;

namespace IRComponents {

struct C_TextureScrollPosition {
    vec2 position_;

    C_TextureScrollPosition(vec2 position)
        : position_(position) {}

    C_TextureScrollPosition(float x, float y)
        : C_TextureScrollPosition(vec2{x, y}) {}

    // Default
    C_TextureScrollPosition()
        : C_TextureScrollPosition(vec2{0.0f, 0.0f}) {}
};

struct C_TextureScrollVelocity {
    vec2 velocity_;

    C_TextureScrollVelocity(vec2 velocity)
        : velocity_(velocity) {}

    C_TextureScrollVelocity(float x, float y)
        : C_TextureScrollVelocity(vec2{x, y}) {}

    // Default
    C_TextureScrollVelocity()
        : C_TextureScrollVelocity(vec2{0.0f, 0.0f}) {}
};

} // namespace IRComponents

#endif /* COMPONENT_TEXTURE_SCROLL_H */

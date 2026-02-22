#ifndef COMPONENT_VIEWPORT_H
#define COMPONENT_VIEWPORT_H

#include <irreden/ir_math.hpp>

using IRMath::vec2;

namespace IRComponents {

struct C_Viewport {

    ivec2 size_;

    C_Viewport(ivec2 size)
        : size_(size) {}

    C_Viewport(int x, int y)
        : C_Viewport(ivec2{x, y}) {}

    // Default
    C_Viewport()
        : C_Viewport(ivec2{0, 0}) {}
};

} // namespace IRComponents

#endif /* COMPONENT_VIEWPORT_STATE_H */

#ifndef COMPONENT_CURSOR_POSITION_H
#define COMPONENT_CURSOR_POSITION_H

#include <irreden/ir_math.hpp>

using IRMath::vec2;

namespace IRComponents {

struct C_CursorPosition {

    double posX_;
    double posY_;
    vec2 hoveredTriangleIndexScreen_;

    C_CursorPosition(double x, double y)
        : posX_{x}
        , posY_{y}
        , hoveredTriangleIndexScreen_{-1, -1} {}

    C_CursorPosition()
        : posX_{0.0}
        , posY_{0.0} {}
};

} // namespace IRComponents

#endif /* COMPONENT_CURSOR_POSITION_H */

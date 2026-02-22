#ifndef COMPONENT_MOUSE_POSITION_H
#define COMPONENT_MOUSE_POSITION_H

#include <irreden/ir_math.hpp>

using IRMath::dvec2;

namespace IRComponents {

struct C_MousePosition {
    dvec2 pos_;

    // default
    C_MousePosition()
        : pos_{0.0} {}
};

} // namespace IRComponents

#endif /* COMPONENT_MOUSE_POSITION_H */

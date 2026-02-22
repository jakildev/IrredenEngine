#ifndef COMPONENT_TRIXEL_CANVAS_ORIGIN_H
#define COMPONENT_TRIXEL_CANVAS_ORIGIN_H

#include <irreden/ir_math.hpp>

namespace IRComponents {

struct C_TrixelCanvasOrigin {
    ivec2 offsetZ1_;

    C_TrixelCanvasOrigin(ivec2 offset)
        : offsetZ1_{offset} {}

    C_TrixelCanvasOrigin() {}
};

} // namespace IRComponents

#endif /* COMPONENT_TRIXEL_CANVAS_ORIGIN_H */

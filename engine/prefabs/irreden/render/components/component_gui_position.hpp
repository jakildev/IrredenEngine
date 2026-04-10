#ifndef COMPONENT_GUI_POSITION_H
#define COMPONENT_GUI_POSITION_H

#include <irreden/ir_math.hpp>

using IRMath::ivec2;

namespace IRComponents {

// 2D position in GUI-canvas trixel coordinates (screen-space, not world-space).
// Origin is the top-left corner of the GUI canvas, +X moves right, +Y moves down.
struct C_GuiPosition {
    ivec2 pos_;

    C_GuiPosition(ivec2 pos)
        : pos_{pos} {}

    C_GuiPosition(int x, int y)
        : C_GuiPosition{ivec2(x, y)} {}

    C_GuiPosition()
        : C_GuiPosition{ivec2(0, 0)} {}
};

} // namespace IRComponents

#endif /* COMPONENT_GUI_POSITION_H */

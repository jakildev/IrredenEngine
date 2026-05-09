#ifndef COMPONENT_HITBOX_2D_GUI_H
#define COMPONENT_HITBOX_2D_GUI_H

#include <irreden/ir_math.hpp>

using IRMath::ivec2;

namespace IRComponents {

// Screen-space hitbox in GUI-canvas trixel coordinates. The AABB extends
// `[pos, pos + size_)` from a sibling `C_GuiPosition` (same coordinate
// frame as `C_TextSegment` text rendering — top-left origin, +X right,
// +Y down). Kept separate from `C_HitBox2D` so the world-space (camera-
// transformed half-extents) and gui-space (camera-independent integer
// trixels) coordinate systems stay un-mixed.
struct C_HitBox2DGui {
    ivec2 size_;
    bool hovered_ = false;

    C_HitBox2DGui()
        : size_{0, 0} {}

    C_HitBox2DGui(ivec2 size)
        : size_{size} {}

    C_HitBox2DGui(int width, int height)
        : size_{width, height} {}
};

} // namespace IRComponents

#endif /* COMPONENT_HITBOX_2D_GUI_H */

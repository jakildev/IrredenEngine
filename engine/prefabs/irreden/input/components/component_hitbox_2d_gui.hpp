#ifndef COMPONENT_HITBOX_2D_GUI_H
#define COMPONENT_HITBOX_2D_GUI_H

#include <irreden/ir_math.hpp>

namespace IRComponents {

// Screen-space AABB hitbox anchored to a sibling C_GuiPosition. Coordinates
// live in **gui canvas trixel** units (top-left origin, +X right, +Y down) —
// the same space the GUI canvas writes glyphs and shapes into. Kept distinct
// from C_HitBox2D (world-anchored, half-extents in framebuffer pixels) so
// the two coordinate systems never get mixed inside one tick.
struct C_HitBox2DGui {
    IRMath::ivec2 size_;
    bool hovered_ = false;

    C_HitBox2DGui()
        : size_{0, 0} {}

    C_HitBox2DGui(IRMath::ivec2 size)
        : size_{size} {}

    C_HitBox2DGui(int width, int height)
        : size_{width, height} {}
};

} // namespace IRComponents

#endif /* COMPONENT_HITBOX_2D_GUI_H */

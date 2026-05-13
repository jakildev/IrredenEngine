#ifndef SYSTEM_GIZMO_HOVER_H
#define SYSTEM_GIZMO_HOVER_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_gizmo_handle.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>

namespace IRSystem {

// Editor gizmo hover detection. Reads the entity-id GPU readback
// (`IRRender::getEntityIdAtMouseTrixel`, filled by f_trixel_to_framebuffer)
// once per frame and stamps `C_GizmoHandle::hover_` accordingly. Also
// writes the visual highlight back onto the sibling `C_ShapeDescriptor` —
// hovered handles get a brightened tint, others get the base color
// captured at spawn. Keeping both ends of the round-trip on this system
// means the renderer needs no per-frame branch on hover_.
//
// Pipeline: INPUT, after INPUT_KEY_MOUSE (the cursor position used by the
// previous frame's framebuffer pass is what `getEntityIdAtMouseTrixel`
// returns; nothing in INPUT mutates it). Must come before GIZMO_DRAG so
// press detection sees a fresh hover_.
template <> struct System<GIZMO_HOVER> {
    IREntity::EntityId cursorEntity_ = IREntity::kNullEntity;

    void beginTick() {
        cursorEntity_ = IRRender::getEntityIdAtMouseTrixel();
    }

    void tick(
        IREntity::EntityId id,
        IRComponents::C_GizmoHandle &handle,
        IRComponents::C_ShapeDescriptor &shape
    ) {
        const bool hovered = (cursorEntity_ != IREntity::kNullEntity) && (id == cursorEntity_);
        handle.hover_ = hovered;
        shape.color_ = hovered ? brighten(handle.baseColor_) : handle.baseColor_;
    }

    static SystemId create() {
        return registerSystem<
            GIZMO_HOVER,
            IRComponents::C_GizmoHandle,
            IRComponents::C_ShapeDescriptor>("GizmoHover");
    }

  private:
    // Lifts each non-alpha channel toward 255 by ~40%. Keeps relative
    // hue (the brighten target is the same direction as the base color),
    // restores cleanly from a saved baseColor_ on the same frame the
    // hover bit clears.
    static IRMath::Color brighten(IRMath::Color c) {
        const auto lift = [](std::uint8_t v) -> std::uint8_t {
            const int boosted = static_cast<int>(v) + (255 - static_cast<int>(v)) * 2 / 5;
            return static_cast<std::uint8_t>(IRMath::min(255, boosted));
        };
        return IRMath::Color{lift(c.red_), lift(c.green_), lift(c.blue_), c.alpha_};
    }
};

} // namespace IRSystem

#endif /* SYSTEM_GIZMO_HOVER_H */

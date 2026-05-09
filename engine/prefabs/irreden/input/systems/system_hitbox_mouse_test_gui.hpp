#ifndef SYSTEM_HITBOX_MOUSE_TEST_GUI_H
#define SYSTEM_HITBOX_MOUSE_TEST_GUI_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_platform.hpp>

#include <irreden/input/components/component_hitbox_2d_gui.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/camera.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

// Mirror of `kIdentityYawEpsilon` in `f_screen_residual_rotate.glsl`. Below
// this magnitude the screen composite is pixel-identical, so the picking
// inverse must skip its rotation too — same rationale as HITBOX_MOUSE_TEST.
inline constexpr float kHitboxGuiIdentityYawEpsilon = 1e-6f;

template <> struct System<HITBOX_MOUSE_TEST_GUI> {
    struct Params {
        // Mouse cursor in gui-canvas trixel units, cached once per frame at
        // beginTick so the per-entity AABB test is a pair of compares.
        vec2 mouseGuiTrixel_{0.0f, 0.0f};
    };

    static SystemId create() {
        auto paramsOwner = std::make_unique<Params>();
        Params *p = paramsOwner.get();

        SystemId systemId = createSystem<C_HitBox2DGui, C_GuiPosition>(
            "HitBoxMouseTestGui",
            [p](C_HitBox2DGui &hitbox, const C_GuiPosition &guiPos) {
                const vec2 origin = vec2(guiPos.pos_);
                const vec2 maxCorner = origin + vec2(hitbox.size_);
                hitbox.hovered_ =
                    p->mouseGuiTrixel_.x >= origin.x && p->mouseGuiTrixel_.x < maxCorner.x &&
                    p->mouseGuiTrixel_.y >= origin.y && p->mouseGuiTrixel_.y < maxCorner.y;
            },
            [p]() {
                auto &framebuffer =
                    IREntity::getComponent<C_TrixelCanvasFramebuffer>("mainFramebuffer");
                const vec2 fbResHalf = vec2(framebuffer.getResolutionPlusBuffer()) * 0.5f;
                const vec2 fbRes = fbResHalf * 2.0f;

                const auto [rasterYaw, residualYaw] = IRPrefab::Camera::getYawSplit();
                // Inverse-rotate the mouse out of the post-composite framebuffer
                // frame into the canvas-pixel frame the GUI canvas was sampled
                // from. Same construction as HITBOX_MOUSE_TEST.
                const vec2 mouseFb = IRRender::getMousePositionOutputView();
                vec2 mouseCanvas;
                if (IRMath::abs(residualYaw) < kHitboxGuiIdentityYawEpsilon) {
                    mouseCanvas = mouseFb;
                } else {
                    const float effectiveAngle = -residualYaw * IRPlatform::kGfx.screenYDirection_;
                    mouseCanvas = IRMath::rotate2D(mouseFb - fbResHalf, effectiveAngle) + fbResHalf;
                }

                // The GUI canvas quad covers the full framebuffer at zoom 1.0 and
                // ignores the camera, so framebuffer pixels divide cleanly by the
                // gui-canvas trixel pixel size: mainCanvas trixel * guiScale.
                const vec2 guiCanvasSize =
                    IRRender::getMainCanvasSizeTrixels() / vec2(IRRender::getGuiScale());
                p->mouseGuiTrixel_ = mouseCanvas * guiCanvasSize / fbRes;
            }
        );
        setSystemParams(systemId, std::move(paramsOwner));
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_HITBOX_MOUSE_TEST_GUI_H */

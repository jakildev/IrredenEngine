#ifndef SYSTEM_HITBOX_MOUSE_TEST_GUI_H
#define SYSTEM_HITBOX_MOUSE_TEST_GUI_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_platform.hpp>

#include <irreden/input/components/component_hitbox_2d_gui.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/camera.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

// Mirrors `kHitboxIdentityYawEpsilon` in `system_hitbox_mouse_test.hpp`
// and `kIdentityYawEpsilon` in `f_screen_residual_rotate.glsl`. Below
// this magnitude the screen composite passes the canvas through pixel-
// identical, so the picking-side inverse must skip its rotation too or
// a yaw=0 hover would shift by sub-pixel rounding.
inline constexpr float kHitboxGuiIdentityYawEpsilon = 1e-6f;

template <> struct System<HITBOX_MOUSE_TEST_GUI> {
    struct Params {
        // Mouse position projected into GUI-canvas-trixel coordinates;
        // computed once per frame in beginTick, read by every per-entity
        // hitbox check.
        vec2 mouseGuiTrixel_ = vec2(0.0f);
    };

    static SystemId create() {
        auto paramsOwner = std::make_unique<Params>();
        Params *p = paramsOwner.get();

        SystemId systemId = createSystem<C_HitBox2DGui, C_GuiPosition>(
            "HitBoxMouseTestGui",
            [p](C_HitBox2DGui &hitbox, const C_GuiPosition &guiPos) {
                const vec2 lo = vec2(guiPos.pos_);
                const vec2 hi = vec2(guiPos.pos_ + hitbox.size_);
                hitbox.hovered_ = p->mouseGuiTrixel_.x >= lo.x && p->mouseGuiTrixel_.x < hi.x &&
                                  p->mouseGuiTrixel_.y >= lo.y && p->mouseGuiTrixel_.y < hi.y;
            },
            [p]() {
                // beginTick lookups assume the "gui" canvas and
                // "mainFramebuffer" entities already exist. Standard render
                // setup creates both before INPUT pipelines run, so a creation
                // that registers HITBOX_MOUSE_TEST_GUI in an INPUT pipeline
                // gets them for free. A creation registering this system
                // before its render pipeline has constructed the GUI canvas
                // would assert here on the first frame.
                EntityId guiCanvas = IRRender::getCanvas("gui");
                auto &canvasTextures = IREntity::getComponent<C_TriangleCanvasTextures>(guiCanvas);
                auto &framebuffer =
                    IREntity::getComponent<C_TrixelCanvasFramebuffer>("mainFramebuffer");

                const vec2 fbRes = vec2(framebuffer.getResolutionPlusBuffer());
                const vec2 guiSize = vec2(canvasTextures.size_);

                // The GUI canvas composites into the same framebuffer that
                // SCREEN_SPACE_RESIDUAL_ROTATE later spins, so its
                // rendered position rotates too. Inverse the residual yaw
                // around the framebuffer center on the same backend-aware
                // axis convention as `system_hitbox_mouse_test`.
                const float residualYaw = IRPrefab::Camera::getResidualYaw();
                vec2 mouseFb = IRRender::getMousePositionOutputView();
                if (IRMath::abs(residualYaw) >= kHitboxGuiIdentityYawEpsilon) {
                    const vec2 fbCenter = fbRes * 0.5f;
                    const float effectiveAngle = -residualYaw * IRPlatform::kGfx.screenYDirection_;
                    mouseFb = IRMath::rotate2D(mouseFb - fbCenter, effectiveAngle) + fbCenter;
                }

                // The GUI canvas texture (size = mainCanvasSize / guiScale)
                // is composited onto a quad covering the entire framebuffer:
                // its `C_TrixelCanvasRenderBehavior` disables camera-pos /
                // zoom / subdivision hookups, so `system_trixel_to_framebuffer`
                // builds an identity-translation + framebuffer-scale model
                // matrix. One framebuffer pixel therefore maps linearly onto
                // `guiSize / fbRes` GUI trixels.
                p->mouseGuiTrixel_ = mouseFb / fbRes * guiSize;
            }
        );

        setSystemParams(systemId, std::move(paramsOwner));
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_HITBOX_MOUSE_TEST_GUI_H */

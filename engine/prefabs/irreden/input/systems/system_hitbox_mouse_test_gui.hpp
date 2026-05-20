#ifndef SYSTEM_HITBOX_MOUSE_TEST_GUI_H
#define SYSTEM_HITBOX_MOUSE_TEST_GUI_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/input/components/component_hitbox_2d_gui.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/camera.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<HITBOX_MOUSE_TEST_GUI> {
    // Mouse position projected into GUI-canvas-trixel coordinates;
    // computed once per frame in beginTick, read by every per-entity
    // hitbox check.
    vec2 mouseGuiTrixel_ = vec2(0.0f);

    void tick(C_HitBox2DGui &hitbox, const C_GuiPosition &guiPos) {
        const vec2 lo = vec2(guiPos.pos_);
        const vec2 hi = vec2(guiPos.pos_ + hitbox.size_);
        hitbox.hovered_ = mouseGuiTrixel_.x >= lo.x && mouseGuiTrixel_.x < hi.x &&
                          mouseGuiTrixel_.y >= lo.y && mouseGuiTrixel_.y < hi.y;
    }

    void beginTick() {
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

        // After T-293 the GUI canvas composites into a framebuffer that no
        // longer post-rotates by residualYaw (SCREEN_SPACE_RESIDUAL_ROTATE
        // is a passthrough; continuous yaw lives in the trixel emit
        // shaders' faceDeform[]). The cursor's framebuffer pixel maps
        // directly onto GUI-canvas-trixel coords without an inverse
        // rotation step.
        const vec2 mouseFb = IRRender::getMousePositionOutputView();

        // The GUI canvas texture (size = mainCanvasSize / guiScale)
        // is composited onto a quad covering the entire framebuffer:
        // its `C_TrixelCanvasRenderBehavior` disables camera-pos /
        // zoom / subdivision hookups, so `system_trixel_to_framebuffer`
        // builds an identity-translation + framebuffer-scale model
        // matrix. One framebuffer pixel therefore maps linearly onto
        // `guiSize / fbRes` GUI trixels.
        mouseGuiTrixel_ = mouseFb / fbRes * guiSize;
    }

    static SystemId create() {
        return registerSystem<HITBOX_MOUSE_TEST_GUI, C_HitBox2DGui, C_GuiPosition>(
            "HitBoxMouseTestGui");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_HITBOX_MOUSE_TEST_GUI_H */

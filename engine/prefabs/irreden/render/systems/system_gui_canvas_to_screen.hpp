#ifndef SYSTEM_GUI_CANVAS_TO_SCREEN_H
#define SYSTEM_GUI_CANVAS_TO_SCREEN_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/gui_canvas.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>

#include <vector>

using namespace IRMath;

namespace IRSystem {

// Draws the GUI canvas onto the default framebuffer at screen resolution, over
// the main framebuffer blit and the sprites. The canvas quad covers exactly the
// area FRAMEBUFFER_TO_SCREEN gives the main framebuffer, so GUI trixel
// coordinates, hit testing and `guiTrixelToScreenPx` are unchanged; only the
// resolution the lattice triangles are rasterized at differs. The quad carries
// no camera sub-pixel residual: the GUI is screen-locked.
//
// Nothing is drawn while the GUI is pixelated
// (`IRPrefab::GuiCanvas::isPixelated`); the main gather then composites the
// canvas into the framebuffer instead.
template <> struct System<GUI_CANVAS_TO_SCREEN> {
    IRRender::Buffer *frameDataBuf_ = nullptr;
    IRRender::ShaderProgram *program_ = nullptr;
    IRRender::VAO *quadVao_ = nullptr;
    IRRender::FrameDataGuiCanvasToScreen frameData_{};
    bool compositesOnScreen_ = false;

    void beginTick() {
        compositesOnScreen_ = IRPrefab::GuiCanvas::compositesOnScreen();
    }

    void
    tick(const IRComponents::C_TriangleCanvasTextures &canvas, const IRComponents::C_GuiCanvas &) {
        if (!compositesOnScreen_) {
            return;
        }
        IR_PROFILE_SCOPE("guiCanvasToScreen");
        const auto &framebuffer =
            IREntity::getComponent<IRComponents::C_TrixelCanvasFramebuffer>("mainFramebuffer");
        const vec2 viewport = vec2(IRRender::getViewport());
        const vec2 outputScale = vec2(IRRender::getOutputScaleFactor());
        const vec2 quadScreenPx = vec2(framebuffer.getResolutionPlusBuffer()) * outputScale;

        mat4 model =
            IRMath::translate(mat4(1.0f), vec3(viewport.x / 2.0f, viewport.y / 2.0f, 0.0f));
        model = IRMath::scale(model, vec3(quadScreenPx.x, quadScreenPx.y, 1.0f));
        frameData_.mpMatrix_ =
            IRMath::ortho(0.0f, viewport.x, 0.0f, viewport.y, -1.0f, 100.0f) * model;
        frameData_.texelScreenPx_ = quadScreenPx / vec2(canvas.size_);
        frameData_.sampleLayout_ = static_cast<int>(canvas.renderedSampleLayout_);
        frameDataBuf_->subData(0, sizeof(IRRender::FrameDataGuiCanvasToScreen), &frameData_);
        frameDataBuf_->bindBase(
            IRRender::BufferTarget::UNIFORM,
            IRRender::kBufferIndex_GuiCanvasToScreenFrameData
        );

        program_->use();
        quadVao_->bind();
        canvas.getTextureColors()->bind(0);
        IRRender::device()->setDepthTest(false);
        IRRender::device()->setDepthWrite(false);
        IRRender::device()->enableBlending();
        IRRender::device()->setPolygonMode(IRRender::PolygonMode::FILL);
        IRRender::device()->drawElements(
            IRRender::DrawMode::TRIANGLES,
            IRShapes2D::kQuadIndicesLength,
            IRRender::IndexType::UNSIGNED_SHORT
        );
        IRRender::device()->disableBlending();
        IRRender::device()->setDepthTest(true);
        IRRender::device()->setDepthWrite(true);
    }

    static SystemId create() {
        IRPrefab::GuiCanvas::ensureDisplaySingleton();
        IRRender::createNamedResource<IRRender::ShaderProgram>(
            "GuiCanvasToScreenProgram",
            std::vector{
                IRRender::ShaderStage{
                    IRRender::kFileVertGuiCanvasToScreen,
                    IRRender::ShaderType::VERTEX
                },
                IRRender::ShaderStage{
                    IRRender::kFileFragGuiCanvasToScreen,
                    IRRender::ShaderType::FRAGMENT
                }
            }
        );
        IRRender::createNamedResource<IRRender::Buffer>(
            "GuiCanvasToScreenFrameData",
            nullptr,
            sizeof(IRRender::FrameDataGuiCanvasToScreen),
            IRRender::BUFFER_STORAGE_DYNAMIC,
            IRRender::BufferTarget::UNIFORM,
            IRRender::kBufferIndex_GuiCanvasToScreenFrameData
        );

        SystemId id = registerSystem<
            GUI_CANVAS_TO_SCREEN,
            IRComponents::C_TriangleCanvasTextures,
            IRComponents::C_GuiCanvas>("GuiCanvasToScreen");
        auto *sys = getSystemParams<System<GUI_CANVAS_TO_SCREEN>>(id);
        sys->frameDataBuf_ =
            IRRender::getNamedResource<IRRender::Buffer>("GuiCanvasToScreenFrameData");
        sys->program_ =
            IRRender::getNamedResource<IRRender::ShaderProgram>("GuiCanvasToScreenProgram");
        sys->quadVao_ = IRRender::getNamedResource<IRRender::VAO>("QuadVAO");
        IRRender::tagGpuStage(id, "guiToScreen");
        return id;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_GUI_CANVAS_TO_SCREEN_H */

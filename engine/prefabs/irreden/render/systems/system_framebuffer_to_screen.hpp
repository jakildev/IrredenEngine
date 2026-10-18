#ifndef SYSTEM_FRAMEBUFFER_TO_SCREEN_H
#define SYSTEM_FRAMEBUFFER_TO_SCREEN_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/components/component_camera_position_2d_iso.hpp>
#include <irreden/render/components/component_texture_scroll.hpp>
#include <irreden/render/framebuffer_screen_math.hpp>
#include <irreden/render/glitch.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>

#include <cstdint>
#include <vector>

using namespace IRMath;

namespace IRSystem {

template <> struct System<FRAMEBUFFER_TO_SCREEN> {
    Buffer *frameDataBuf_ = nullptr;
    ShaderProgram *program_ = nullptr;
    VAO *quadVao_ = nullptr;
    FrameDataFramebuffer frameData_{};
    // Screen-pixel half of the camera's sub-pixel decomposition for this
    // frame; the main framebuffer's blit is the only one that carries it.
    vec2 cameraScreenResidual_{0.0f};
    // True for a RENDER tick on which `FRAMEBUFFER_GLITCH` already cleared the
    // default target and drew the screen base: this pass then binds, clears
    // and draws nothing, so the target is filled exactly once.
    bool screenAlreadyComposited_ = false;
    // Main-framebuffer blits drawn since registration; the headless probe
    // that proves an enabled glitch frame replaces this blit reads it.
    std::uint64_t ordinaryBlitCount_ = 0;

    void tick(
        const C_TrixelCanvasFramebuffer &framebuffer,
        const C_WorldTransform &cameraWorldXform,
        const C_Name &name
    ) {
        if (screenAlreadyComposited_) {
            return;
        }
        const bool isMainFramebuffer = name.name_ == "mainFramebuffer";
        framebuffer.bindTextures(0, 1);
        frameData_.mvpMatrix = detail::framebufferScreenMvp(
            framebuffer.getResolutionPlusBuffer(),
            cameraWorldXform.translation_,
            cameraScreenResidual_,
            isMainFramebuffer
        );
        frameDataBuf_->subData(0, sizeof(FrameDataFramebuffer), &frameData_);
        IRRender::device()->setPolygonMode(PolygonMode::FILL);
        IRRender::device()->drawArrays(DrawMode::TRIANGLES, 0, 6);
        if (isMainFramebuffer) {
            ++ordinaryBlitCount_;
        }
    }

    void beginTick() {
        screenAlreadyComposited_ = IRPrefab::Glitch::screenCompositedForCurrentRenderTick();
        if (screenAlreadyComposited_) {
            return;
        }
        cameraScreenResidual_ = vec2(
            IRPrefab::Camera::screenResidual(
                IRRender::getEffectiveCameraIso(),
                IRRender::getCameraZoom(),
                IRRender::getOutputScaleFactor()
            )
        );
        IRRender::device()->bindDefaultFramebuffer();
        IRRender::device()->clearDefaultFramebuffer();
        // Slot 2 is shared with GUI_CANVAS_TO_SCREEN's UBO; each screen pass
        // re-asserts its own binding before it draws.
        frameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FramebufferFrameDataUniform);
        program_->use();
        quadVao_->bind();
    }

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "FramebufferToScreenProgram",
            std::vector{
                ShaderStage{IRRender::kFileVertFramebufferToScreen, ShaderType::VERTEX},
                ShaderStage{IRRender::kFileFragFramebufferToScreen, ShaderType::FRAGMENT}
            }
        );
        IRRender::createNamedResource<Buffer>(
            "FramebufferToScreenFrameData",
            nullptr,
            sizeof(FrameDataFramebuffer),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_FramebufferFrameDataUniform
        );

        SystemId id = registerSystem<
            FRAMEBUFFER_TO_SCREEN,
            C_TrixelCanvasFramebuffer,
            C_WorldTransform,
            C_Name>("FramebufferToScreen");
        auto *sys = getSystemParams<System<FRAMEBUFFER_TO_SCREEN>>(id);
        sys->frameDataBuf_ = IRRender::getNamedResource<Buffer>("FramebufferToScreenFrameData");
        sys->program_ = IRRender::getNamedResource<ShaderProgram>("FramebufferToScreenProgram");
        sys->quadVao_ = IRRender::getNamedResource<VAO>("QuadVAOArrays");
        IRRender::tagGpuStage(id, "fbToScreen");
        return id;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_FRAMEBUFFER_TO_SCREEN_H */

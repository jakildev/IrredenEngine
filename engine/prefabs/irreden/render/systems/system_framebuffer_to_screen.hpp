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

// The GPU work of one ordinary blit tick, and everything it reads from the
// RenderManager. `beginFrame` issues one `beginScreen`, then `encodeBlit` one
// `drawFramebuffer` per framebuffer; a RENDER tick the glitch pass already
// composited issues neither. `ScreenBlitGpuEncoder` encodes them; the
// headless probes record them.
class ScreenBlitEncoder {
  public:
    // Binds and clears the default target, then binds the blit's program,
    // frame data and quad.
    virtual void beginScreen() = 0;
    virtual void drawFramebuffer(
        const C_TrixelCanvasFramebuffer &framebuffer, vec3 cameraPosition, bool isMainFramebuffer
    ) = 0;

  protected:
    ~ScreenBlitEncoder() = default;
};

class ScreenBlitGpuEncoder final : public ScreenBlitEncoder {
  public:
    ScreenBlitGpuEncoder(
        Buffer &frameDataBuf, ShaderProgram &program, VAO &quadVao, vec2 &cameraScreenResidual
    )
        : m_frameDataBuf{frameDataBuf}
        , m_program{program}
        , m_quadVao{quadVao}
        , m_cameraScreenResidual{cameraScreenResidual} {}

    void beginScreen() override {
        m_cameraScreenResidual = vec2(
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
        m_frameDataBuf.bindBase(BufferTarget::UNIFORM, kBufferIndex_FramebufferFrameDataUniform);
        m_program.use();
        m_quadVao.bind();
    }

    void drawFramebuffer(
        const C_TrixelCanvasFramebuffer &framebuffer, vec3 cameraPosition, bool isMainFramebuffer
    ) override {
        framebuffer.bindTextures(0, 1);
        FrameDataFramebuffer frameData{};
        frameData.mvpMatrix = detail::framebufferScreenMvp(
            framebuffer.getResolutionPlusBuffer(),
            cameraPosition,
            m_cameraScreenResidual,
            isMainFramebuffer
        );
        m_frameDataBuf.subData(0, sizeof(FrameDataFramebuffer), &frameData);
        IRRender::device()->setPolygonMode(PolygonMode::FILL);
        IRRender::device()->drawArrays(DrawMode::TRIANGLES, 0, 6);
    }

  private:
    Buffer &m_frameDataBuf;
    ShaderProgram &m_program;
    VAO &m_quadVao;
    vec2 &m_cameraScreenResidual;
};

template <> struct System<FRAMEBUFFER_TO_SCREEN> {
    Buffer *frameDataBuf_ = nullptr;
    ShaderProgram *program_ = nullptr;
    VAO *quadVao_ = nullptr;
    // Screen-pixel half of the camera's sub-pixel decomposition for this
    // frame; the main framebuffer's blit is the only one that carries it.
    vec2 cameraScreenResidual_{0.0f};
    // True for a RENDER tick on which `FRAMEBUFFER_GLITCH` already cleared the
    // default target and drew the screen base: this pass then binds, clears
    // and draws nothing, so the target is filled exactly once.
    bool screenAlreadyComposited_ = false;
    // Main-framebuffer blits drawn since registration; the headless probes
    // that prove which pass drew the screen base read it.
    std::uint64_t ordinaryBlitCount_ = 0;

    void beginFrame(ScreenBlitEncoder &encoder) {
        screenAlreadyComposited_ = IRPrefab::Glitch::screenCompositedForCurrentRenderTick();
        if (screenAlreadyComposited_) {
            return;
        }
        encoder.beginScreen();
    }

    void encodeBlit(
        ScreenBlitEncoder &encoder,
        const C_TrixelCanvasFramebuffer &framebuffer,
        const C_WorldTransform &cameraWorldXform,
        const C_Name &name
    ) {
        if (screenAlreadyComposited_) {
            return;
        }
        const bool isMainFramebuffer = name.name_ == "mainFramebuffer";
        encoder.drawFramebuffer(framebuffer, cameraWorldXform.translation_, isMainFramebuffer);
        if (isMainFramebuffer) {
            ++ordinaryBlitCount_;
        }
    }

    void beginTick() {
        ScreenBlitGpuEncoder encoder = gpuEncoder();
        beginFrame(encoder);
    }

    void tick(
        const C_TrixelCanvasFramebuffer &framebuffer,
        const C_WorldTransform &cameraWorldXform,
        const C_Name &name
    ) {
        ScreenBlitGpuEncoder encoder = gpuEncoder();
        encodeBlit(encoder, framebuffer, cameraWorldXform, name);
    }

    ScreenBlitGpuEncoder gpuEncoder() {
        return ScreenBlitGpuEncoder{*frameDataBuf_, *program_, *quadVao_, cameraScreenResidual_};
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

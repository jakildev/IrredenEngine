#ifndef SYSTEM_FRAMEBUFFER_TO_SCREEN_H
#define SYSTEM_FRAMEBUFFER_TO_SCREEN_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
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

// The GPU work of one blit tick, and everything it reads from the
// RenderManager. `beginFrame` issues one `beginScreen`, then `encodeBlit` one
// draw per framebuffer: `drawGlitchOutput` for the main framebuffer on a
// RENDER tick the glitch pass published, else `drawFramebuffer`.
// `ScreenBlitGpuEncoder` encodes them; the headless probes record them.
class ScreenBlitEncoder {
  public:
    // Binds and clears the default target, then binds the blit's program,
    // frame data and quad.
    virtual void beginScreen() = 0;
    // Places @p framebuffer on screen from its own colour.
    virtual void drawFramebuffer(
        const C_TrixelCanvasFramebuffer &framebuffer, vec3 cameraPosition, bool isMainFramebuffer
    ) = 0;
    // Places the main @p framebuffer on screen from @p output, the glitch
    // pass's same-size twin of it drawn this tick.
    virtual void drawGlitchOutput(
        const C_TrixelCanvasFramebuffer &framebuffer, const Texture2D *output, vec3 cameraPosition
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
        draw(framebuffer, framebuffer.getTextureColor(), cameraPosition, isMainFramebuffer);
    }

    void drawGlitchOutput(
        const C_TrixelCanvasFramebuffer &framebuffer, const Texture2D *output, vec3 cameraPosition
    ) override {
        IR_ASSERT(output != nullptr, "a published glitch pass owns its output image");
        draw(framebuffer, *output, cameraPosition, true);
    }

  private:
    void draw(
        const C_TrixelCanvasFramebuffer &framebuffer,
        const Texture2D &color,
        vec3 cameraPosition,
        bool isMainFramebuffer
    ) {
        color.bind(0);
        framebuffer.getTextureDepth().bind(1);
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
    // True on a RENDER tick the glitch pass drew its output: the main
    // framebuffer is then presented from that output, not its composite.
    bool glitchPublished_ = false;
    const Texture2D *glitchOutput_ = nullptr;
    // Main-framebuffer blits drawn since registration, from the composite and
    // from the glitch output; the headless probes that prove which image the
    // screen shows read them.
    std::uint64_t ordinaryBlitCount_ = 0;
    std::uint64_t glitchOutputBlitCount_ = 0;

    void beginFrame(ScreenBlitEncoder &encoder) {
        glitchPublished_ = IRPrefab::Glitch::outputPublishedForCurrentRenderTick();
        glitchOutput_ = glitchPublished_ ? IRPrefab::Glitch::presentedMainColorOrNull() : nullptr;
        encoder.beginScreen();
    }

    void encodeBlit(
        ScreenBlitEncoder &encoder,
        const C_TrixelCanvasFramebuffer &framebuffer,
        const C_WorldTransform &cameraWorldXform,
        const C_Name &name
    ) {
        const bool isMainFramebuffer = name.name_ == "mainFramebuffer";
        if (isMainFramebuffer && glitchPublished_) {
            encoder.drawGlitchOutput(framebuffer, glitchOutput_, cameraWorldXform.translation_);
            ++glitchOutputBlitCount_;
            return;
        }
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

#ifndef SYSTEM_FRAMEBUFFER_TO_SCREEN_H
#define SYSTEM_FRAMEBUFFER_TO_SCREEN_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/components/component_camera_position_2d_iso.hpp>
#include <irreden/render/components/component_texture_scroll.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>

#include <vector>

using namespace IRMath;

// TODO: effects at this stage like blur, etc!!

// MODIFY THIS TO JUST BE THE 2D SPRITE RENDERER.
// CHANGE FRAMEBUFFERS TO RENDERBUFFERS

namespace IRSystem {

template <> struct System<FRAMEBUFFER_TO_SCREEN> {
    Buffer *frameDataBuf_ = nullptr;
    ShaderProgram *program_ = nullptr;
    VAO *quadVao_ = nullptr;
    FrameDataFramebuffer frameData_{};

    void tick(
        const C_TrixelCanvasFramebuffer &framebuffer,
        const C_WorldTransform &cameraWorldXform,
        const C_Name &name
    ) {
        framebuffer.bindTextures(0, 1);
        frameData_.mvpMatrix = calcProjectionMatrix() * calcModelMatrix(
                                                            framebuffer.getResolution(),
                                                            framebuffer.getResolutionPlusBuffer(),
                                                            cameraWorldXform.translation_,
                                                            IRRender::getCameraPosition2DIso(),
                                                            name.name_
                                                        );
        frameDataBuf_->subData(0, sizeof(FrameDataFramebuffer), &frameData_);
        IRRender::device()->setPolygonMode(PolygonMode::FILL);
        IRRender::device()->drawArrays(DrawMode::TRIANGLES, 0, 6);
    }

    void beginTick() {
        bindDefaultFramebuffer();
        clearDefaultFramebuffer();
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

  private:
    static mat4 calcModelMatrix(
        ivec2 resolution,
        ivec2 resolutionPlusBuffer,
        vec3 cameraPosition,
        vec2 cameraPositionIso,
        std::string name
    ) {
        const ivec2 scaleFactor = IRRender::getOutputScaleFactor();

        // also known as screen center
        float xOffset = IRRender::getViewport().x / 2.0f;
        float yOffset = IRRender::getViewport().y / 2.0f;
        vec2 offset = vec2(xOffset, yOffset) + isoDeltaToScreenDelta(
                                                   pos3DtoPos2DIso(cameraPosition),
                                                   IRRender::getTriangleStepSizeScreen()
                                               );

        mat4 model = mat4(1.0f);

        // Screen-pixel half of the anti-vibration decomposition (see
        // `IRMath::cameraSubPixelOffsets`). `TRIXEL_TO_FRAMEBUFFER` consumes
        // the matching `framebufferGamePxOffset_` from the same helper —
        // both terms derive from one floor() chain so they cannot disagree
        // at game-pixel boundaries. The name check matches the framebuffer
        // entity created by `RenderManager` (see `kFramebuffer` prefab,
        // `render_manager.cpp:47`); only the main framebuffer carries the
        // camera-driven sub-pixel residual.
        if (name == "mainFramebuffer") {
            const IRMath::CameraSubPixelOffsets sub = IRMath::cameraSubPixelOffsets(
                cameraPositionIso,
                IRRender::getCameraZoom(),
                scaleFactor
            );
            offset += vec2(sub.screenPxResidual_);
        }
        model = IRMath::translate(model, vec3(offset.x, offset.y, 0.0f));
        model = IRMath::scale(
            model,
            vec3(
                resolutionPlusBuffer.x * scaleFactor.x,
                resolutionPlusBuffer.y * scaleFactor.y,
                1.0f
            )
        );
        return model;
    }

    static mat4 calcProjectionMatrix() {
        mat4 projection = IRMath::ortho(
            0.0f,
            (float)IRRender::getViewport().x,
            0.0f,
            (float)IRRender::getViewport().y,
            -1.0f,
            100.0f
        );
        return projection;
    }

    static void bindDefaultFramebuffer() {
        IRRender::device()->bindDefaultFramebuffer();
    }

    static void clearDefaultFramebuffer() {
        IRRender::device()->clearDefaultFramebuffer();
    }
};

} // namespace IRSystem

#endif /* SYSTEM_FRAMEBUFFER_TO_SCREEN_H */

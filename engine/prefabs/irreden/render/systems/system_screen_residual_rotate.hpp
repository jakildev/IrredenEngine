#ifndef SYSTEM_SCREEN_SPACE_RESIDUAL_ROTATE_H
#define SYSTEM_SCREEN_SPACE_RESIDUAL_ROTATE_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/components/component_camera_position_2d_iso.hpp>
#include <irreden/render/components/component_texture_scroll.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>
#include <irreden/render/camera.hpp>

#include <vector>

using namespace IRMath;

namespace IRSystem {

/// Final composite-to-screen pass that applies the residual yaw rotation
/// (visualYaw - rasterYaw, in [-pi/4, pi/4]) to the trixel framebuffer
/// before blitting to the swapchain. Drop-in replacement for
/// FRAMEBUFFER_TO_SCREEN: identical pixel output when residualYaw is zero
/// (passthrough branch in the fragment shader bypasses the bilinear blend),
/// rotated bilinear sample otherwise.
template <> struct System<SCREEN_SPACE_RESIDUAL_ROTATE> {
    Buffer *frameDataBuf_ = nullptr;
    ShaderProgram *program_ = nullptr;
    VAO *quadVao_ = nullptr;
    FrameDataScreenResidualRotate frameData_{};

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
        // residualYaw_ is left at its default 0 — the stage is a passthrough
        // after T-293, with residual yaw folded into the trixel emit
        // shaders' faceDeform[] (camera.hpp no longer drives the screen-
        // space residual composite). Field preserved for UBO layout
        // backward compat.
        frameDataBuf_->subData(0, sizeof(FrameDataScreenResidualRotate), &frameData_);
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
            "ScreenSpaceResidualRotateProgram",
            std::vector{
                ShaderStage{IRRender::kFileVertScreenResidualRotate, ShaderType::VERTEX},
                ShaderStage{IRRender::kFileFragScreenResidualRotate, ShaderType::FRAGMENT}
            }
        );
        IRRender::createNamedResource<Buffer>(
            "ScreenSpaceResidualRotateFrameData",
            nullptr,
            sizeof(FrameDataScreenResidualRotate),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_FrameDataScreenResidualRotate
        );

        SystemId id = registerSystem<
            SCREEN_SPACE_RESIDUAL_ROTATE,
            C_TrixelCanvasFramebuffer,
            C_WorldTransform,
            C_Name>("ScreenSpaceResidualRotate");
        auto *sys = getSystemParams<System<SCREEN_SPACE_RESIDUAL_ROTATE>>(id);
        sys->frameDataBuf_ =
            IRRender::getNamedResource<Buffer>("ScreenSpaceResidualRotateFrameData");
        sys->program_ =
            IRRender::getNamedResource<ShaderProgram>("ScreenSpaceResidualRotateProgram");
        sys->quadVao_ = IRRender::getNamedResource<VAO>("QuadVAOArrays");
        IRRender::tagGpuStage(id, "screenSpaceResidualRotate");
        return id;
    }

  private:
    // The model/projection helpers below mirror System<FRAMEBUFFER_TO_SCREEN>
    // exactly so the two stages produce identical pixel positions when their
    // shaders agree (residualYaw=0 path). Extracting them into a shared
    // header is intentionally deferred: the helpers will diverge once the
    // residual-rotate pass starts handling rotation-induced canvas
    // overscan, at which point the contract is no longer "identical to
    // FRAMEBUFFER_TO_SCREEN". Any timing instrumentation added here should
    // use T-066's SystemManager::TickObserver pattern, not per-system code.
    static mat4 calcModelMatrix(
        ivec2 resolution,
        ivec2 resolutionPlusBuffer,
        vec3 cameraPosition,
        vec2 cameraPositionIso,
        std::string name
    ) {
        const ivec2 scaleFactor = IRRender::getOutputScaleFactor();
        float xOffset = IRRender::getViewport().x / 2.0f;
        float yOffset = IRRender::getViewport().y / 2.0f;
        vec2 offset = vec2(xOffset, yOffset) + isoDeltaToScreenDelta(
                                                   pos3DtoPos2DIso(cameraPosition),
                                                   IRRender::getTriangleStepSizeScreen()
                                               );

        mat4 model = mat4(1.0f);
        // Anti-vibration decomposition — see `IRMath::cameraSubPixelOffsets`
        // and the parallel call sites in `TRIXEL_TO_FRAMEBUFFER` /
        // `FRAMEBUFFER_TO_SCREEN`. Only the main framebuffer carries the
        // camera-driven sub-pixel residual; other framebuffers (if added)
        // composite from canvas/world space without a screen residual.
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
        return IRMath::ortho(
            0.0f,
            (float)IRRender::getViewport().x,
            0.0f,
            (float)IRRender::getViewport().y,
            -1.0f,
            100.0f
        );
    }

    static void bindDefaultFramebuffer() {
        IRRender::device()->bindDefaultFramebuffer();
    }

    static void clearDefaultFramebuffer() {
        IRRender::device()->clearDefaultFramebuffer();
    }
};

} // namespace IRSystem

#endif /* SYSTEM_SCREEN_SPACE_RESIDUAL_ROTATE_H */

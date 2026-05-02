#ifndef SYSTEM_SCREEN_SPACE_RESIDUAL_ROTATE_H
#define SYSTEM_SCREEN_SPACE_RESIDUAL_ROTATE_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/components/component_camera_position_2d_iso.hpp>
#include <irreden/render/components/component_texture_scroll.hpp>
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
    struct Params {
        Buffer *frameDataBuf_ = nullptr;
        ShaderProgram *program_ = nullptr;
        VAO *quadVao_ = nullptr;
        FrameDataScreenResidualRotate frameData_{};
    };

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

        auto paramsOwner = std::make_unique<Params>();
        Params *p = paramsOwner.get();
        p->frameDataBuf_ = IRRender::getNamedResource<Buffer>("ScreenSpaceResidualRotateFrameData");
        p->program_ = IRRender::getNamedResource<ShaderProgram>("ScreenSpaceResidualRotateProgram");
        p->quadVao_ = IRRender::getNamedResource<VAO>("QuadVAOArrays");

        SystemId systemId = createSystem<C_TrixelCanvasFramebuffer, C_Position3D, C_Name>(
            "ScreenSpaceResidualRotate",
            [p](const C_TrixelCanvasFramebuffer &framebuffer,
                const C_Position3D &cameraPosition,
                const C_Name &name) {
                framebuffer.bindTextures(0, 1);
                p->frameData_.mvpMatrix =
                    calcProjectionMatrix() * calcModelMatrix(
                                                 framebuffer.getResolution(),
                                                 framebuffer.getResolutionPlusBuffer(),
                                                 cameraPosition.pos_,
                                                 IRRender::getCameraPosition2DIso(),
                                                 name.name_
                                             );
                p->frameData_.residualYaw = IRPrefab::Camera::getResidualYaw();
                p->frameDataBuf_->subData(
                    0, sizeof(FrameDataScreenResidualRotate), &p->frameData_
                );
                IRRender::device()->setPolygonMode(PolygonMode::FILL);
                IRRender::device()->drawArrays(DrawMode::TRIANGLES, 0, 6);
            },
            [p]() {
                bindDefaultFramebuffer();
                clearDefaultFramebuffer();
                p->program_->use();
                p->quadVao_->bind();
            }
        );

        setSystemParams(systemId, std::move(paramsOwner));
        return systemId;
    }

  private:
    // The model/projection helpers below mirror System<FRAMEBUFFER_TO_SCREEN>
    // exactly so the two stages produce identical pixel positions when their
    // shaders agree (residualYaw=0 path). Extracting them into a shared
    // header is intentionally deferred: the helpers will diverge once the
    // residual-rotate pass starts handling rotation-induced canvas
    // overscan, at which point the contract is no longer "identical to
    // FRAMEBUFFER_TO_SCREEN".
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
        vec2 offset = vec2(xOffset, yOffset) +
                      isoDeltaToScreenDelta(
                          pos3DtoPos2DIso(cameraPosition),
                          IRRender::getTriangleStepSizeScreen()
                      );

        mat4 model = mat4(1.0f);
        if (name == "main") {
            vec2 framebufferPositionOffset = IRMath::floor(
                IRMath::fract(
                    IRMath::pos2DIsoToPos2DGameResolution(
                        IRMath::fract(cameraPositionIso),
                        IRRender::getCameraZoom()
                    )
                ) *
                IRPlatform::kIsoToScreenSign * vec2(scaleFactor)
            );
            offset += framebufferPositionOffset;
        } else if (name == "background") {
            offset += IRPlatform::kIsoToScreenSign * vec2(scaleFactor);
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

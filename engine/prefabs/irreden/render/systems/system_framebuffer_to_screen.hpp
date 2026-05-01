#ifndef SYSTEM_FRAMEBUFFER_TO_SCREEN_H
#define SYSTEM_FRAMEBUFFER_TO_SCREEN_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/components/component_camera_position_2d_iso.hpp>
#include <irreden/render/components/component_texture_scroll.hpp>
#include <irreden/render/gpu_stage_timing.hpp>

#include <vector>

using namespace IRMath;

// TODO: effects at this stage like blur, etc!!

// MODIFY THIS TO JUST BE THE 2D SPRITE RENDERER.
// CHANGE FRAMEBUFFERS TO RENDERBUFFERS

namespace IRSystem {

template <> struct System<FRAMEBUFFER_TO_SCREEN> {
    struct Params {
        Buffer *frameDataBuf_ = nullptr;
        ShaderProgram *program_ = nullptr;
        VAO *quadVao_ = nullptr;
        FrameDataFramebuffer frameData_{};
    };

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

        auto paramsOwner = std::make_unique<Params>();
        Params *p = paramsOwner.get();
        p->frameDataBuf_ = IRRender::getNamedResource<Buffer>("FramebufferToScreenFrameData");
        p->program_ = IRRender::getNamedResource<ShaderProgram>("FramebufferToScreenProgram");
        p->quadVao_ = IRRender::getNamedResource<VAO>("QuadVAOArrays");

        SystemId systemId = createSystem<C_TrixelCanvasFramebuffer, C_Position3D, C_Name>(
            "FramebufferToScreen",
            [p](const C_TrixelCanvasFramebuffer &framebuffer,
                const C_Position3D &cameraPosition,
                const C_Name &name) {
                auto &timing = IRRender::gpuStageTiming();
                IRRender::TimePoint t0;
                if (timing.enabled_) { IRRender::device()->finish(); t0 = IRRender::SteadyClock::now(); }

                framebuffer.bindTextures(0, 1);
                p->frameData_.mvpMatrix =
                    calcProjectionMatrix() * calcModelMatrix(
                                                 framebuffer.getResolution(),
                                                 framebuffer.getResolutionPlusBuffer(),
                                                 cameraPosition.pos_,
                                                 IRRender::getCameraPosition2DIso(),
                                                 name.name_
                                             );
                p->frameDataBuf_->subData(0, sizeof(FrameDataFramebuffer), &p->frameData_);
                IRRender::device()->setPolygonMode(PolygonMode::FILL);
                IRRender::device()->drawArrays(DrawMode::TRIANGLES, 0, 6);

                if (timing.enabled_) { IRRender::device()->finish(); timing.fbToScreenMs_ += IRRender::elapsedMs(t0, IRRender::SteadyClock::now()); }
            },
            [p]() {
                IRRender::gpuStageTiming().fbToScreenMs_ = 0.0f;
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
        vec2 offset = vec2(xOffset, yOffset) +
                      isoDeltaToScreenDelta(
                          pos3DtoPos2DIso(cameraPosition),
                          IRRender::getTriangleStepSizeScreen()
                      );

        mat4 model = mat4(1.0f);

        // Pixel perfect part
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
            // Need to offset by one pixel here but not exactly sure why atm
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

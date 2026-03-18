#ifndef SYSTEM_SPRITE_TO_SCREEN_Hs
#define SYSTEM_SPRITE_TO_SCREEN_H

#include <irreden/ir_ecs.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/components/component_camera_position_2d_iso.hpp>
#include <irreden/render/components/component_texture_scroll.hpp>

using namespace IRMath;

// TODO: effects at this stage like blur, etc!!

// MODIFY THIS TO JUST BE THE 2D SPRITE RENDERER.
// CHANGE FRAMEBUFFERS TO RENDERBUFFERS

namespace IRSystem {

template <> struct System<SPRITE_TO_SCREEN> {
    static SystemId create() {
        static FrameDataFramebuffer frameData{};
        IRRender::createNamedResource<Buffer>(
            "FramebufferToScreenFrameData",
            nullptr,
            sizeof(FrameDataFramebuffer),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_FramebufferFrameDataUniform
        );

        return createSystem<C_TrixelCanvasFramebuffer, C_Position3D, C_Name>(
            "FramebufferToScreen",
            [](const C_TrixelCanvasFramebuffer &framebuffer,
               const C_Position3D &cameraPosition,
               const C_Name &name) {
                framebuffer.bindTextures(0, 1);
                frameData.mvpMatrix =
                    calcProjectionMatrix() * calcModelMatrix(
                                                 framebuffer.getResolution(),
                                                 framebuffer.getResolutionPlusBuffer(),
                                                 cameraPosition.pos_,
                                                 IRRender::getCameraPosition2DIso(),
                                                 name.name_
                                             );
                IRRender::getNamedResource<Buffer>("FramebufferToScreenFrameData")
                    ->subData(0, sizeof(FrameDataFramebuffer), &frameData);
                IRRender::device()->setPolygonMode(PolygonMode::FILL);
                IRRender::device()->drawArrays(DrawMode::TRIANGLES, 0, 6);
            },
            []() {
                bindDefaultFramebuffer();
                clearDefaultFramebuffer();
                IRRender::getNamedResource<ShaderProgram>("FramebufferToScreenProgram")->use();
                IRRender::getNamedResource<VAO>("QuadVAOArrays")->bind();
            }
        );
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
                      (pos3DtoPos2DScreen(cameraPosition, IRRender::getTriangleStepSizeScreen()) *
                       vec2(-1, 1));

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
                vec2(1, -1) * vec2(scaleFactor)
            );
            offset += framebufferPositionOffset;
        } else if (name == "background") {
            // Need to offset by one pixel here but not exactly sure why atm
            offset += vec2(1.0f, -1.0f) * vec2(scaleFactor);
        } else {
            // offset += (
            //     IRRender::getCameraPositionScreen() *
            //     vec2(1, -1)
            // );
        }
        model = IRMath::translate(model, vec3(offset.x, offset.y, 0.0f));
        model = IRMath::scale(
            model,
            vec3(resolutionPlusBuffer.x * scaleFactor, resolutionPlusBuffer.y * scaleFactor, 1.0f)
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

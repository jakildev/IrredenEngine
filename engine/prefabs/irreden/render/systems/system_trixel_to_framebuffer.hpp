/*
 * Project: Irreden Engine
 * File: system_rendering_canvas_to_framebuffer.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_TRIXEL_TO_FRAMEBUFFER_H
#define SYSTEM_TRIXEL_TO_FRAMEBUFFER_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_ecs.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/components/component_texture_scroll.hpp>
#include <irreden/render/components/component_frame_data_trixel_to_framebuffer.hpp>

using namespace IRComponents;
using namespace IRRender;
using namespace IRMath;

// ADD ABLILITY TO TEXTURE OVER FACES!

namespace IRECS {

    template <>
    struct System<TRIXEL_TO_FRAMEBUFFER> {
        static SystemId create() {
            IRRender::createNamedResource<ShaderProgram>(
                "CanvasToFramebufferProgram",
                std::vector{
                    ShaderStage{
                        IRRender::kFileVertTrixelToFramebuffer,
                        GL_VERTEX_SHADER
                    }.getHandle(),
                    ShaderStage{
                        IRRender::kFileFragTrixelToFramebuffer,
                        GL_FRAGMENT_SHADER
                    }.getHandle()
                }
            );
            IRRender::createNamedResource<Buffer>(
                "TrixelToFramebufferFrameData",
                nullptr,
                sizeof(FrameDataTrixelToFramebuffer),
                GL_DYNAMIC_STORAGE_BIT,
                GL_UNIFORM_BUFFER,
                kBufferIndex_FrameDataUniformIsoTriangles
            );

            return createSystem<
                C_TriangleCanvasTextures
            >(
                "CanvasToFramebuffer",
                [](
                    const C_TriangleCanvasTextures& triangleCanvasTextures
                    // No parent params here, handled below. Need a way
                    // to standardize this though.
                )
                {
                    triangleCanvasTextures.bind(0, 1);

                    ENG_API->glPolygonMode( GL_FRONT_AND_BACK, GL_FILL);
                    ENG_API->glDrawElements(
                        GL_TRIANGLES,
                        IRShapes2D::kQuadIndicesLength,
                        GL_UNSIGNED_SHORT,
                        nullptr
                    );

                },
                []() {
                    IRRender::getNamedResource<ShaderProgram>(
                        "CanvasToFramebufferProgram"
                    )->use();
                    IRRender::getNamedResource<VAO>(
                        "QuadVAO"
                    )->bind();
                },
                nullptr,
                RelationParams<
                    C_TrixelCanvasFramebuffer,
                    C_FrameDataTrixelToFramebuffer,
                    C_ZoomLevel
                >{
                    Relation::CHILD_OF
                },
                [](
                    const C_TrixelCanvasFramebuffer& framebuffer,
                    C_FrameDataTrixelToFramebuffer& frameData,
                    const C_ZoomLevel& zoomLevel
                )
                {
                    vec2 framebufferResolution =
                        vec2(framebuffer.getResolutionPlusBuffer());
                    frameData.frameData_.canvasZoomLevel_ =
                        IRRender::getCameraZoom() *
                        zoomLevel.zoom_;
                    frameData.frameData_.cameraTrixelOffset_ = IRRender::getCameraPosition2DIso();
                    frameData.frameData_.textureOffset_ = vec2(0);
                    frameData.frameData_.mouseHoveredTriangleIndex_ =
                        IRRender::mouseTrixelPositionWorld();
                    frameData.frameData_.mpMatrix_ =
                        calcProjectionMatrix(
                            framebufferResolution
                        ) *
                        calcModelMatrix(
                            framebufferResolution,
                            frameData.frameData_.cameraTrixelOffset_,
                            frameData.frameData_.canvasZoomLevel_
                        );
                    frameData.updateFrameData(
                        IRRender::getNamedResource<Buffer>(
                        "TrixelToFramebufferFrameData"
                    ));
                    framebuffer.bindFramebuffer();
                    // framebuffer.framebuffer_.second->getTextureColor().saveAsPNG(
                    //     "../save_files/test.png"
                    // );
                    framebuffer.clear();
                }

            );
        }
        private:
        static mat4 calcProjectionMatrix(const vec2& resolution) {
            mat4 projection = glm::ortho(
                0.0f,
                resolution.x,
                0.0f,
                resolution.y,
                -1.0f,
                100.0f
            );
            return projection;
        }

        static mat4 calcModelMatrix(
            const vec2& resolution,
            const vec2& cameraPositionIso,
            const vec2& zoomLevel
        )
        {
            vec2 isoPixelOffset =
                glm::floor(
                    IRMath::pos2DIsoToPos2DGameResolution(
                        glm::fract(cameraPositionIso),
                        zoomLevel
                    )
                ) *
                vec2(1, -1);
            mat4 model = mat4(1.0f);
            model = glm::translate(
                model,
                glm::vec3(
                    resolution.x / 2 + isoPixelOffset.x,
                    resolution.y / 2 + isoPixelOffset.y,
                    0.0f
                )
            );
            model = glm::scale(
                model,
                glm::vec3(
                    resolution.x *
                        zoomLevel.x,
                    resolution.y *
                        zoomLevel.y,
                    1.0f
                )
            );
            return model;
        }
    };

} // namespace System

#endif /* SYSTEM_TRIXEL_TO_FRAMEBUFFER_H */

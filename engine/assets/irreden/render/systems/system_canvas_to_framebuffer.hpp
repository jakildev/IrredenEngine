/*
 * Project: Irreden Engine
 * File: system_rendering_canvas_to_framebuffer.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_CANVAS_TO_FRAMEBUFFER_H
#define SYSTEM_CANVAS_TO_FRAMEBUFFER_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_ecs.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_position_2d.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/render/components/component_triangle_framebuffer.hpp>
#include <irreden/render/components/component_texture_scroll.hpp>
#include <irreden/update/systems/system_update_screen_view.hpp>

#include <glm/gtc/matrix_transform.hpp> // not this here

using namespace IRComponents;
using namespace IRRender;
using namespace IRMath;

// ADD ABLILITY TO TEXTURE OVER FACES!

namespace IRECS {

    template <>
    struct System<RENDERING_CANVAS_TO_FRAMEBUFFER> {
        static SystemId create() {
            static FrameDataIsoTriangles frameData{
                .mpMatrix_ = mat4(1)
            };
            Buffer* vertexBuffer = IRRender::createResource<Buffer>(
                IRShapes2D::kQuadVertices,
                sizeof(IRShapes2D::kQuadVertices),
                0
            ).second;
            Buffer* indexBuffer = IRRender::createResource<Buffer>(
                IRShapes2D::kQuadIndices,
                sizeof(IRShapes2D::kQuadIndices),
                0
            ).second;
            IRRender::createNamedResource<VAO>(
                "CanvasToFramebufferVAO",
                vertexBuffer->getHandle(),
                indexBuffer->getHandle(),
                1,
                &kAttrFloat2
            );
            IRRender::createNamedResource<ShaderProgram>(
                "CanvasToFramebufferProgram",
                std::vector{
                    ShaderStage{
                        IRRender::kFileVertIsoTrianglesScreen,
                        GL_VERTEX_SHADER
                    }.getHandle(),
                    ShaderStage{
                        IRRender::kFileFragIsoTrianglesScreen,
                        GL_FRAGMENT_SHADER
                    }.getHandle()
                }
            );

            IRRender::createNamedResource<Buffer>(
                "CanvasToFramebufferFrameData",
                nullptr,
                sizeof(FrameDataIsoTriangles),
                GL_DYNAMIC_STORAGE_BIT,
                GL_UNIFORM_BUFFER,
                kBufferIndex_FrameDataUniformIsoTriangles
            );
            return createSystem<
                C_TriangleCanvasTextures,
                C_TriangleCanvasFramebuffer,
                C_CameraPosition2DIso,
                C_ZoomLevel
            >(
                "CanvasToFramebuffer",
                [](
                    const C_TriangleCanvasTextures& triangleCanvasTextures,
                    const C_TriangleCanvasFramebuffer& framebuffer,
                    const C_CameraPosition2DIso& cameraPosition,
                    const C_ZoomLevel& zoomLevel
                )
                {
                    vec2 framebufferResolution =
                        vec2(framebuffer.getResolutionPlusBuffer());
                    mat4 projection = glm::ortho(
                        0.0f,
                        framebufferResolution.x,
                        0.0f,
                        framebufferResolution.y,
                        -1.0f,
                        100.0f
                    );
                    framebuffer.bindFramebuffer();
                    framebuffer.clear();

                    frameData.canvasZoomLevel_ =
                        IRRender::getCameraZoom() *
                        zoomLevel.zoom_;

                    mat4 model = mat4(1.0f);
                    model = glm::translate(
                        model,
                        glm::vec3(
                            framebufferResolution.x / 2,
                            framebufferResolution.y / 2,
                            0.0f
                        )
                    );
                    model = glm::scale(
                        model,
                        glm::vec3(
                            framebufferResolution.x *
                                frameData.canvasZoomLevel_.x,
                            framebufferResolution.y *
                                frameData.canvasZoomLevel_.y,
                            1.0f
                        )
                    );
                    frameData.mpMatrix_ = projection * model;
                    frameData.canvasOffset_ = cameraPosition.pos_;

                    frameData.textureOffset_ = vec2(0);
                    IRRender::getNamedResource<Buffer>(
                        "CanvasToFramebufferFrameData"
                    )->subData(
                        0,
                        sizeof(FrameDataIsoTriangles),
                        &frameData
                    );
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
                        "CanvasToFramebufferVAO"
                    )->bind();
                }
            );
        }
    };

} // namespace System

#endif /* SYSTEM_CANVAS_TO_FRAMEBUFFER_H */
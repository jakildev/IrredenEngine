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

#include <irreden/common/components/component_position_2d.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/components/component_texture_scroll.hpp>
#include <irreden/update/systems/system_update_screen_view.hpp>

#include <glm/gtc/matrix_transform.hpp> // not this here

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
            return createSystem<
                C_TriangleCanvasTextures,
                C_TrixelCanvasFramebuffer,
                C_FrameDataTrixelToFramebuffer
            >(
                "CanvasToFramebuffer",
                [](
                    const C_TriangleCanvasTextures& triangleCanvasTextures,
                    const C_TrixelCanvasFramebuffer& framebuffer,
                    const C_FrameDataTrixelToFramebuffer& frameData
                )
                {
                    frameData.updateFrameData();
                    framebuffer.bindFramebuffer();
                    framebuffer.clear();
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
                }
            );
        }
    };

} // namespace System

#endif /* SYSTEM_TRIXEL_TO_FRAMEBUFFER_H */

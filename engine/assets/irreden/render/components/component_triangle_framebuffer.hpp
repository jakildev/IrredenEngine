/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_rendering_triangle_framebuffer.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_TRIANGLE_FRAMEBUFFER_H
#define COMPONENT_TRIANGLE_FRAMEBUFFER_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/rendering_rm.hpp>
#include <irreden/render/framebuffer.hpp>

using namespace IRMath;
using namespace IRRender;

namespace IRComponents {

    // TODO make a renderbuffer instead to test performance
    struct C_TriangleCanvasFramebuffer {
        std::pair<ResourceId, Framebuffer*> framebuffer_;

        C_TriangleCanvasFramebuffer(
            ivec2 size,
            ivec2 extraPixelBuffer = ivec2(0, 0)
        )
        :   framebuffer_{
                IRRender::createResource<
                    IRRender::Framebuffer
                >(
                    size,
                    extraPixelBuffer,
                    GL_RGBA8,
                    GL_DEPTH24_STENCIL8
                )
            }
        {

        }

        C_TriangleCanvasFramebuffer() {}

        void onDestroy() {
            IRRender::destroyResource<Framebuffer>(
                framebuffer_.first
            );
        }

        void bindFramebuffer() const {
            framebuffer_.second->bind();
        }

        void clear() const {
            framebuffer_.second->clear();
        }

        void draw() {

        }

        void bindTextures(int bindingColor, int bindingDepth) const {
            framebuffer_.second->getTextureColor().bind(bindingColor);
            framebuffer_.second->getTextureDepth().bind(bindingDepth);
        }

        const ivec2 getResolution() const {
            return framebuffer_.second->getResolution();
        }
        const ivec2 getResolutionPlusBuffer() const {
            return framebuffer_.second->getResolutionPlusBuffer();
        }

    };

} // IRComponents

#endif /* COMPONENT_RENDERING_TRIANGLE_CANVAS_TEXTURES_H */

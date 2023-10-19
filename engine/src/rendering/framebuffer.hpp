/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\rendering\framebuffer.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "ir_rendering.hpp"
#include "texture.hpp"
#include "buffer.hpp"
#include "vao.hpp"

#include "../math/ir_math.hpp"

using namespace IRMath;

namespace IRRendering {

    class Framebuffer {
    public:
        Framebuffer(
            ivec2 resolution,
            ivec2 extraPixelBuffer,
            GLenum formatColor,
            GLenum formatDepthStencil);
        ~Framebuffer();

        void bind() const;
        void unbind();
        void clear() const;

        inline const Texture2D& getTextureColor() const {
            return m_textureColor;
        }
        inline const Texture2D& getTextureDepth() const {
            return m_textureDepth;
        }
        inline const ivec2 getResolution() const {
            return m_resolution;
        }
        inline const ivec2 getResolutionPlusBuffer() const {
            return m_resolutionPlusBuffer;
        }
    private:
        const ivec2 m_resolution;
        const ivec2 m_extraPixelBuffer;
        const ivec2 m_resolutionPlusBuffer;
        GLuint m_id;
        Texture2D m_textureColor;
        Texture2D m_textureDepth;

        void checkSuccess();
    };

} // namespace IRRendering

#endif /* FRAMEBUFFER_H */
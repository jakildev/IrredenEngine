/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\rendering\texture.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef TEXTURE_H
#define TEXTURE_H

#include <glad/glad.h>
#include "../rendering/ir_gl_api.hpp"
#include "../profiling/logger_spd.hpp"
#include "voxel_data.hpp"
#include "../math/ir_math.hpp"

using namespace IRMath;

namespace IRRendering {

    class Texture2D {
    public:
        Texture2D(
            GLenum type,
            unsigned int width,
            unsigned int height,
            GLenum internalFormat,
            GLint wrap = GL_REPEAT,
            GLint filter = GL_NEAREST,
            int alignment = 1
        );
        ~Texture2D();
        Texture2D(const Texture2D& other);
        Texture2D& operator=(const Texture2D& other);
        Texture2D(Texture2D&& other);
        Texture2D& operator=(Texture2D&& other);
        inline unsigned int getWidth() const { return m_width; }
        inline unsigned int getHeight() const { return m_height; }
        inline uvec2 getSize() const { return uvec2(m_width, m_height); }

        GLuint getHandle() const;
        void bind(GLuint unit = 0) const;
        void bindImage(
            GLuint unit = 0,
            GLenum access = GL_READ_WRITE,
            GLenum format = GL_RGBA32F,
            GLint level = 0,
            GLboolean layered = GL_FALSE,
            GLint layer = 0
        ) const;
        void setParameteri(GLenum pname, GLint param);
        void subImage2D(
            GLint xoffset,
            GLint yoffset,
            GLsizei width,
            GLsizei height,
            GLenum format,
            GLenum type,
            const void* data
        );
        void clear(GLenum format, GLenum type, const void* data);
    private:
        GLuint m_handle;
        unsigned int m_width, m_height;
    };

    class Texture3D {
    public:
        Texture3D(
            GLenum type,
            unsigned int width,
            unsigned int height,
            unsigned int depth,
            GLenum internalFormat,
            GLint wrap = GL_REPEAT,
            GLint filter = GL_NEAREST
        );
        ~Texture3D();
        GLuint getHandle() const;
        void bind(GLuint unit = 0);
        void setParameteri(GLenum pname, GLint param);
        void subImage3D(
            GLsizei width,
            GLsizei height,
            GLsizei depth,
            GLenum format,
            GLenum type,
            const void* data
        );
        // void subVoxels3D(
        //     const VoxelData& voxels,
        //     ivec3 offset = ivec3(0, 0, 0)
        // );
    private:
        GLuint m_handle;
        unsigned int m_width, m_height, m_depth;
    };

} // namespace IRRendering

#endif /* TEXTURE_H */

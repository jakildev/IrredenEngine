#ifndef TEXTURE_H
#define TEXTURE_H

#include <irreden/ir_profile.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/opengl/opengl_types.hpp>
#include <irreden/render/ir_gl_api.hpp>

using namespace IRMath;

namespace IRRender {

    class ITexture2D {
    public:
        virtual ~ITexture2D() = default;
        inline uvec2 getSize() const { return ivec2(m_width, m_height); }
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
        void getSubImage2D(
            GLint xoffset,
            GLint yoffset,
            GLsizei width,
            GLsizei height,
            GLenum format,
            GLenum type,
            void* data
        )   const;
        void clear(GLenum format, GLenum type, const void* data);
        void* getData() const;
        void saveAsPNG(const char* file) const;
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

    private:
        GLuint m_handle;
        unsigned int m_width, m_height, m_depth;
    };

} // namespace IRRender

#endif /* TEXTURE_H */

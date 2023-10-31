/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\rendering\texture.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/render/texture.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>

namespace IRRender {

    Texture2D::Texture2D(
        GLenum type,
        unsigned int width,
        unsigned int height,
        GLenum internalFormat,
        GLint wrap,
        GLint filter,
        int alignment
    )
    :   m_width(width),
        m_height(height)
    {
        ENG_API->glCreateTextures(type, 1, &m_handle);
        ENG_API->glTextureStorage2D(m_handle, 1, internalFormat, width, height);
        ENG_API->glTextureParameteri(m_handle, GL_TEXTURE_WRAP_S, wrap);
        ENG_API->glTextureParameteri(m_handle, GL_TEXTURE_WRAP_T, wrap);
        ENG_API->glTextureParameteri(m_handle, GL_TEXTURE_MIN_FILTER, filter);
        ENG_API->glTextureParameteri(m_handle, GL_TEXTURE_MAG_FILTER, filter);
        ENG_API->glPixelStorei(
            GL_PACK_ALIGNMENT,
            alignment
        );
        ENG_API->glPixelStorei(
            GL_UNPACK_ALIGNMENT,
            alignment
        );

        IRProfile::engLogInfo("Created texture 2D: {}", m_handle);
    }

    Texture2D::~Texture2D() {
        IRProfile::engLogInfo("Deleting texture 2D: {}", m_handle);
        ENG_API->glDeleteTextures(1, &this->m_handle);
    }

    Texture2D::Texture2D(const Texture2D& other) {
        *this = other;
    }

    Texture2D& Texture2D::operator=(const Texture2D& other) {
        if (this != &other) {
            m_handle = other.m_handle;
            m_width = other.m_width;
            m_height = other.m_height;
        }
        return *this;
    }

    Texture2D::Texture2D(Texture2D&& other) {
        *this = std::move(other);
    }

    Texture2D& Texture2D::operator=(Texture2D&& other) {
        if (this != &other) {
            m_handle = other.m_handle;
            m_width = other.m_width;
            m_height = other.m_height;
            // other.m_handle = 0;
        }
        return *this;
    }

    void Texture2D::bind(GLuint unit) const {
        ENG_API->glBindTextureUnit(unit, m_handle);
    }

    void Texture2D::bindImage(
        GLuint unit,
        GLenum access,
        GLenum format,
        GLint level,
        GLboolean layered,
        GLint layer
    ) const
    {
        // TODO: Figure out why its not eng ENG_API
        // UPDATE: Need to update gl_wrap/funcs_list.txt I think
        glBindImageTexture(
            unit,
            m_handle,
            level,
            layered,
            layer,
            access,
            format
        );
    }

    GLuint Texture2D::getHandle() const {
        return m_handle;
    }

    void Texture2D::setParameteri(GLenum pname, GLint param) {
        ENG_API->glTextureParameteri(m_handle, pname, param);
    }

    void Texture2D::subImage2D(
        GLint xoffset,
        GLint yoffset,
        GLsizei width,
        GLsizei height,
        GLenum format,
        GLenum type,
        const void* data
    ) {
        ENG_API->glTextureSubImage2D(
            m_handle,
            0,
            xoffset,
            yoffset,
            width,
            height,
            format,
            type,
            data
        );
    }
    void Texture2D::clear(GLenum format, GLenum type, const void* data) {
        IRProfile::profileFunction(IR_PROFILER_COLOR_RENDER);
        // TODO: Figure out why its not eng ENG_API
        // UPDATE: Need to update gl_wrap/funcs_list.txt I think
        glClearTexImage(
            m_handle,
            0,
            format,
            type,
            data
        );
    }

    // Texture3D

    Texture3D::Texture3D(
        GLenum type,
        unsigned int width,
        unsigned int height,
        unsigned int depth,
        GLenum internalFormat,
        GLint wrap,
        GLint filter
    )
    :   m_width(width),
        m_height(height),
        m_depth(depth)
    {
        IRProfile::engLogInfo("Creating GL Texture (3D) handle={}", m_handle);
        ENG_API->glCreateTextures(type, 1, &m_handle);
        ENG_API->glTextureStorage3D(m_handle, 1, internalFormat, width, height, depth);
        ENG_API->glTextureParameteri(m_handle, GL_TEXTURE_WRAP_S, wrap);
        ENG_API->glTextureParameteri(m_handle, GL_TEXTURE_WRAP_T, wrap);
        ENG_API->glTextureParameteri(m_handle, GL_TEXTURE_WRAP_R, wrap);
        ENG_API->glTextureParameteri(m_handle, GL_TEXTURE_MIN_FILTER, filter);
        ENG_API->glTextureParameteri(m_handle, GL_TEXTURE_MAG_FILTER, filter);
        ENG_API->glPixelStorei(
            GL_UNPACK_ALIGNMENT,
            1
        );
    }

    Texture3D::~Texture3D() {
        IRProfile::engLogInfo("Deleting GL Texture (3D) handle={}", m_handle);
        ENG_API->glDeleteTextures(1, &m_handle);
    }

    void Texture3D::bind(GLuint unit) {
        ENG_API->glBindTextureUnit(unit, m_handle);
    }

    GLuint Texture3D::getHandle() const {
        return m_handle;
    }

    void Texture3D::setParameteri(GLenum pname, GLint param) {
        ENG_API->glTextureParameteri(m_handle, pname, param);
    }

    void Texture3D::subImage3D(
        GLsizei width,
        GLsizei height,
        GLsizei depth,
        GLenum format,
        GLenum type,
        const void* data
    )
    {
        IRProfile::profileFunction(IR_PROFILER_COLOR_RENDER);
        ENG_API->glTextureSubImage3D(
            m_handle,
            0,
            0,
            0,
            0,
            width,
            height,
            depth,
            format,
            type,
            data
        );
    }

} // namespace IRRender
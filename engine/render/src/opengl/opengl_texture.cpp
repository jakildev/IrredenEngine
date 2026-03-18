#include <irreden/ir_profile.hpp>

#include <irreden/render/ir_gl_api.hpp>
#include <irreden/render/opengl/opengl_types.hpp>
#include <irreden/render/texture.hpp>

namespace IRRender {

namespace {
GLsizei glTypeSize(GLenum type) {
    switch (type) {
        case GL_UNSIGNED_BYTE:
        case GL_BYTE:
            return 1;
        case GL_UNSIGNED_SHORT:
        case GL_SHORT:
        case GL_HALF_FLOAT:
            return 2;
        case GL_UNSIGNED_INT:
        case GL_INT:
        case GL_FLOAT:
            return 4;
        default:
            return 4;
    }
}

GLsizei glFormatComponents(GLenum format) {
    switch (format) {
        case GL_RED:
        case GL_RED_INTEGER:
            return 1;
        case GL_RG:
        case GL_RG_INTEGER:
            return 2;
        case GL_RGB:
        case GL_RGB_INTEGER:
        case GL_BGR:
        case GL_BGR_INTEGER:
            return 3;
        case GL_RGBA:
        case GL_RGBA_INTEGER:
        case GL_BGRA:
        case GL_BGRA_INTEGER:
            return 4;
        default:
            return 4;
    }
}
} // namespace

class OpenGLTexture2DImpl final : public Texture2DImpl {
  public:
    OpenGLTexture2DImpl(
        TextureKind type,
        unsigned int width,
        unsigned int height,
        TextureFormat internalFormat,
        TextureWrap wrap,
        TextureFilter filter,
        int alignment
    )
        : m_width(width)
        , m_height(height) {
        ENG_API->glCreateTextures(toGLTextureKind(type), 1, &m_handle);
        ENG_API->glTextureStorage2D(
            m_handle,
            1,
            toGLTextureFormat(internalFormat),
            static_cast<GLsizei>(width),
            static_cast<GLsizei>(height)
        );
        ENG_API->glTextureParameteri(m_handle, GL_TEXTURE_WRAP_S, toGLTextureWrap(wrap));
        ENG_API->glTextureParameteri(m_handle, GL_TEXTURE_WRAP_T, toGLTextureWrap(wrap));
        ENG_API->glTextureParameteri(m_handle, GL_TEXTURE_MIN_FILTER, toGLTextureFilter(filter));
        ENG_API->glTextureParameteri(m_handle, GL_TEXTURE_MAG_FILTER, toGLTextureFilter(filter));
        ENG_API->glPixelStorei(GL_PACK_ALIGNMENT, alignment);
        ENG_API->glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    }

    ~OpenGLTexture2DImpl() override {
        ENG_API->glDeleteTextures(1, &m_handle);
    }

    uvec2 getSize() const override {
        return uvec2(m_width, m_height);
    }

    std::uint32_t getHandle() const override {
        return m_handle;
    }

    void *getNativeTexture() const override {
        return nullptr;
    }

    std::uint32_t getNativePixelFormat() const override {
        return 0;
    }

    void bind(std::uint32_t unit) const override {
        ENG_API->glBindTextureUnit(unit, m_handle);
    }

    void bindImage(
        std::uint32_t unit,
        TextureAccess access,
        TextureFormat format,
        int level,
        bool layered,
        int layer
    ) const override {
        glBindImageTexture(
            unit,
            m_handle,
            level,
            layered ? GL_TRUE : GL_FALSE,
            layer,
            toGLTextureAccess(access),
            toGLTextureFormat(format)
        );
    }

    void uploadSubImage2D(
        int xoffset,
        int yoffset,
        int width,
        int height,
        PixelDataFormat format,
        PixelDataType type,
        const void *data
    ) override {
        ENG_API->glTextureSubImage2D(
            m_handle,
            0,
            xoffset,
            yoffset,
            width,
            height,
            toGLPixelDataFormat(format),
            toGLPixelDataType(type),
            data
        );
    }

    void readSubImage2D(
        int xoffset,
        int yoffset,
        int width,
        int height,
        PixelDataFormat format,
        PixelDataType type,
        void *data
    ) const override {
        const GLenum glFormat = toGLPixelDataFormat(format);
        const GLenum glType = toGLPixelDataType(type);
        const GLsizei bufSize =
            static_cast<GLsizei>(width * height * glFormatComponents(glFormat) * glTypeSize(glType));
        glGetTextureSubImage(m_handle, 0, xoffset, yoffset, 0, width, height, 1, glFormat, glType, bufSize, data);
    }

    void clear(PixelDataFormat format, PixelDataType type, const void *data) override {
        glClearTexImage(m_handle, 0, toGLPixelDataFormat(format), toGLPixelDataType(type), data);
    }

  private:
    GLuint m_handle = 0;
    unsigned int m_width = 0;
    unsigned int m_height = 0;
};

class OpenGLTexture3DImpl final : public Texture3DImpl {
  public:
    OpenGLTexture3DImpl(
        TextureKind type,
        unsigned int width,
        unsigned int height,
        unsigned int depth,
        TextureFormat internalFormat,
        TextureWrap wrap,
        TextureFilter filter
    ) {
        ENG_API->glCreateTextures(toGLTextureKind(type), 1, &m_handle);
        ENG_API->glTextureStorage3D(
            m_handle,
            1,
            toGLTextureFormat(internalFormat),
            static_cast<GLsizei>(width),
            static_cast<GLsizei>(height),
            static_cast<GLsizei>(depth)
        );
        ENG_API->glTextureParameteri(m_handle, GL_TEXTURE_WRAP_S, toGLTextureWrap(wrap));
        ENG_API->glTextureParameteri(m_handle, GL_TEXTURE_WRAP_T, toGLTextureWrap(wrap));
        ENG_API->glTextureParameteri(m_handle, GL_TEXTURE_WRAP_R, toGLTextureWrap(wrap));
        ENG_API->glTextureParameteri(m_handle, GL_TEXTURE_MIN_FILTER, toGLTextureFilter(filter));
        ENG_API->glTextureParameteri(m_handle, GL_TEXTURE_MAG_FILTER, toGLTextureFilter(filter));
        ENG_API->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    }

    ~OpenGLTexture3DImpl() override {
        ENG_API->glDeleteTextures(1, &m_handle);
    }

    std::uint32_t getHandle() const override {
        return m_handle;
    }

    void *getNativeTexture() const override {
        return nullptr;
    }

    std::uint32_t getNativePixelFormat() const override {
        return 0;
    }

    void bind(std::uint32_t unit) override {
        ENG_API->glBindTextureUnit(unit, m_handle);
    }

    void uploadSubImage3D(
        int width,
        int height,
        int depth,
        PixelDataFormat format,
        PixelDataType type,
        const void *data
    ) override {
        ENG_API->glTextureSubImage3D(
            m_handle,
            0,
            0,
            0,
            0,
            width,
            height,
            depth,
            toGLPixelDataFormat(format),
            toGLPixelDataType(type),
            data
        );
    }

  private:
    GLuint m_handle = 0;
};

std::unique_ptr<Texture2DImpl> createTexture2DImpl(
    TextureKind type,
    unsigned int width,
    unsigned int height,
    TextureFormat internalFormat,
    TextureWrap wrap,
    TextureFilter filter,
    int alignment
) {
    return std::make_unique<OpenGLTexture2DImpl>(
        type, width, height, internalFormat, wrap, filter, alignment
    );
}

std::unique_ptr<Texture3DImpl> createTexture3DImpl(
    TextureKind type,
    unsigned int width,
    unsigned int height,
    unsigned int depth,
    TextureFormat internalFormat,
    TextureWrap wrap,
    TextureFilter filter
) {
    return std::make_unique<OpenGLTexture3DImpl>(
        type, width, height, depth, internalFormat, wrap, filter
    );
}

} // namespace IRRender

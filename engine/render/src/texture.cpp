#include <irreden/render/image_data.hpp>
#include <irreden/render/texture.hpp>

#include <utility>

namespace IRRender {

std::unique_ptr<Texture2DImpl> createTexture2DImpl(
    TextureKind type,
    unsigned int width,
    unsigned int height,
    TextureFormat internalFormat,
    TextureWrap wrap,
    TextureFilter filter,
    int alignment
);
std::unique_ptr<Texture3DImpl> createTexture3DImpl(
    TextureKind type,
    unsigned int width,
    unsigned int height,
    unsigned int depth,
    TextureFormat internalFormat,
    TextureWrap wrap,
    TextureFilter filter
);

Texture2D::Texture2D(
    TextureKind type,
    unsigned int width,
    unsigned int height,
    TextureFormat internalFormat,
    TextureWrap wrap,
    TextureFilter filter,
    int alignment
)
    : m_impl(createTexture2DImpl(type, width, height, internalFormat, wrap, filter, alignment)) {}

Texture2D::~Texture2D() = default;
Texture2D::Texture2D(Texture2D &&other) noexcept = default;
Texture2D &Texture2D::operator=(Texture2D &&other) noexcept = default;

uvec2 Texture2D::getSize() const {
    return m_impl->getSize();
}

std::uint32_t Texture2D::getHandle() const {
    return m_impl->getHandle();
}

void *Texture2D::getNativeTexture() const {
    return m_impl->getNativeTexture();
}

std::uint32_t Texture2D::getNativePixelFormat() const {
    return m_impl->getNativePixelFormat();
}

void Texture2D::bind(std::uint32_t unit) const {
    m_impl->bind(unit);
}

void Texture2D::bindAsImage(
    std::uint32_t unit,
    TextureAccess access,
    TextureFormat format,
    int level,
    bool layered,
    int layer
) const {
    m_impl->bindImage(unit, access, format, level, layered, layer);
}

void Texture2D::subImage2D(
    int xoffset,
    int yoffset,
    int width,
    int height,
    PixelDataFormat format,
    PixelDataType type,
    const void *data
) {
    m_impl->uploadSubImage2D(xoffset, yoffset, width, height, format, type, data);
}

void Texture2D::getSubImage2D(
    int xoffset,
    int yoffset,
    int width,
    int height,
    PixelDataFormat format,
    PixelDataType type,
    void *data
) const {
    m_impl->readSubImage2D(xoffset, yoffset, width, height, format, type, data);
}

void Texture2D::clear(PixelDataFormat format, PixelDataType type, const void *data) {
    m_impl->clear(format, type, data);
}

void Texture2D::saveAsPNG(const char *file) const {
    const uvec2 size = getSize();
    std::vector<unsigned char> data(size.x * size.y * 4);
    getSubImage2D(0, 0, size.x, size.y, PixelDataFormat::RGBA, PixelDataType::UNSIGNED_BYTE, data.data());
    writePNG(file, size.x, size.y, 4, data.data());
}

Texture3D::Texture3D(
    TextureKind type,
    unsigned int width,
    unsigned int height,
    unsigned int depth,
    TextureFormat internalFormat,
    TextureWrap wrap,
    TextureFilter filter
)
    : m_impl(createTexture3DImpl(type, width, height, depth, internalFormat, wrap, filter)) {}

Texture3D::~Texture3D() = default;
Texture3D::Texture3D(Texture3D &&other) noexcept = default;
Texture3D &Texture3D::operator=(Texture3D &&other) noexcept = default;

std::uint32_t Texture3D::getHandle() const {
    return m_impl->getHandle();
}

void Texture3D::bind(std::uint32_t unit) {
    m_impl->bind(unit);
}

void Texture3D::subImage3D(
    int width, int height, int depth, PixelDataFormat format, PixelDataType type, const void *data
) {
    m_impl->uploadSubImage3D(width, height, depth, format, type, data);
}

} // namespace IRRender
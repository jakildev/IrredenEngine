#ifndef TEXTURE_H
#define TEXTURE_H

#include <irreden/ir_math.hpp>
#include <irreden/render/ir_render_enums.hpp>

#include <cstdint>
#include <memory>

using namespace IRMath;

namespace IRRender {

class Texture2DImpl {
  public:
    virtual ~Texture2DImpl() = default;
    virtual uvec2 getSize() const = 0;
    virtual std::uint32_t getHandle() const = 0;
    virtual void *getNativeTexture() const = 0;
    virtual std::uint32_t getNativePixelFormat() const = 0;
    virtual void bind(std::uint32_t unit) const = 0;
    virtual void bindImage(
        std::uint32_t unit,
        TextureAccess access,
        TextureFormat format,
        int level,
        bool layered,
        int layer
    ) const = 0;
    virtual void uploadSubImage2D(
        int xoffset,
        int yoffset,
        int width,
        int height,
        PixelDataFormat format,
        PixelDataType type,
        const void *data
    ) = 0;
    virtual void readSubImage2D(
        int xoffset,
        int yoffset,
        int width,
        int height,
        PixelDataFormat format,
        PixelDataType type,
        void *data
    ) const = 0;
    virtual void clear(PixelDataFormat format, PixelDataType type, const void *data) = 0;
};

class Texture3DImpl {
  public:
    virtual ~Texture3DImpl() = default;
    virtual std::uint32_t getHandle() const = 0;
    virtual void *getNativeTexture() const = 0;
    virtual std::uint32_t getNativePixelFormat() const = 0;
    virtual void bind(std::uint32_t unit) = 0;
    virtual void uploadSubImage3D(
        int width,
        int height,
        int depth,
        PixelDataFormat format,
        PixelDataType type,
        const void *data
    ) = 0;
};

class Texture2D {
  public:
    Texture2D(
        TextureKind type,
        unsigned int width,
        unsigned int height,
        TextureFormat internalFormat,
        TextureWrap wrap = TextureWrap::REPEAT,
        TextureFilter filter = TextureFilter::NEAREST,
        int alignment = 1
    );
    ~Texture2D();
    Texture2D(Texture2D &&other) noexcept;
    Texture2D &operator=(Texture2D &&other) noexcept;
    Texture2D(const Texture2D &) = delete;
    Texture2D &operator=(const Texture2D &) = delete;

    uvec2 getSize() const;
    std::uint32_t getHandle() const;
    void *getNativeTexture() const;
    std::uint32_t getNativePixelFormat() const;
    void bind(std::uint32_t unit = 0) const;
    void bindAsImage(
        std::uint32_t unit = 0,
        TextureAccess access = TextureAccess::READ_WRITE,
        TextureFormat format = TextureFormat::RGBA32F,
        int level = 0,
        bool layered = false,
        int layer = 0
    ) const;
    void subImage2D(
        int xoffset,
        int yoffset,
        int width,
        int height,
        PixelDataFormat format,
        PixelDataType type,
        const void *data
    );
    void getSubImage2D(
        int xoffset,
        int yoffset,
        int width,
        int height,
        PixelDataFormat format,
        PixelDataType type,
        void *data
    ) const;
    void clear(PixelDataFormat format, PixelDataType type, const void *data);
    void saveAsPNG(const char *file) const;

  private:
    std::unique_ptr<Texture2DImpl> m_impl;
};

class Texture3D {
  public:
    Texture3D(
        TextureKind type,
        unsigned int width,
        unsigned int height,
        unsigned int depth,
        TextureFormat internalFormat,
        TextureWrap wrap = TextureWrap::REPEAT,
        TextureFilter filter = TextureFilter::NEAREST
    );
    ~Texture3D();
    Texture3D(Texture3D &&other) noexcept;
    Texture3D &operator=(Texture3D &&other) noexcept;
    Texture3D(const Texture3D &) = delete;
    Texture3D &operator=(const Texture3D &) = delete;

    std::uint32_t getHandle() const;
    void bind(std::uint32_t unit = 0);
    void subImage3D(
        int width,
        int height,
        int depth,
        PixelDataFormat format,
        PixelDataType type,
        const void *data
    );

  private:
    std::unique_ptr<Texture3DImpl> m_impl;
};

} // namespace IRRender

#endif /* TEXTURE_H */

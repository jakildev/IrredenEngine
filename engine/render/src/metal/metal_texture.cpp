#include <irreden/render/texture.hpp>
#include <irreden/render/metal/metal_runtime.hpp>
#include <irreden/ir_profile.hpp>

#include <cstring>
#include <vector>

namespace IRRender {

namespace {

MTL::PixelFormat toMetalTextureFormat(TextureFormat format) {
    switch (format) {
        case TextureFormat::RGBA8:
            return MTL::PixelFormatRGBA8Unorm;
        case TextureFormat::RGBA32F:
            return MTL::PixelFormatRGBA32Float;
        case TextureFormat::R32I:
            return MTL::PixelFormatR32Sint;
        case TextureFormat::RG32UI:
            return MTL::PixelFormatRG32Uint;
        case TextureFormat::DEPTH24_STENCIL8:
            return MTL::PixelFormatDepth32Float_Stencil8;
    }
    return MTL::PixelFormatRGBA8Unorm;
}

std::size_t pixelSizeBytes(PixelDataFormat format, PixelDataType type) {
    const std::size_t typeSize = [&]() -> std::size_t {
        switch (type) {
            case PixelDataType::UNSIGNED_BYTE:
                return 1;
            case PixelDataType::INT32:
            case PixelDataType::UINT32:
            case PixelDataType::FLOAT32:
                return 4;
        }
        return 4;
    }();

    const std::size_t components = [&]() -> std::size_t {
        switch (format) {
            case PixelDataFormat::RGBA:
                return 4;
            case PixelDataFormat::RED_INTEGER:
                return 1;
            case PixelDataFormat::RG_INTEGER:
                return 2;
        }
        return 4;
    }();

    return typeSize * components;
}

std::uint32_t nextMetalTextureHandle() {
    static std::uint32_t s_nextHandle = 1;
    return s_nextHandle++;
}

MTL::TextureUsage defaultTextureUsage(TextureFormat format) {
    if (format == TextureFormat::DEPTH24_STENCIL8) {
        return MTL::TextureUsageRenderTarget;
    }
    MTL::TextureUsage usage = static_cast<MTL::TextureUsage>(
        MTL::TextureUsageShaderRead |
        MTL::TextureUsageShaderWrite |
        MTL::TextureUsageRenderTarget
    );
    if (format == TextureFormat::R32I) {
        usage = static_cast<MTL::TextureUsage>(usage | MTL::TextureUsageShaderAtomic);
    }
    return usage;
}

} // namespace

class MetalTexture2DImpl final : public Texture2DImpl {
  public:
    MetalTexture2DImpl(unsigned int width, unsigned int height, TextureFormat format)
        : m_size(width, height)
        , m_pixelFormat(toMetalTextureFormat(format))
        , m_handle(nextMetalTextureHandle()) {
        auto *descriptor = MTL::TextureDescriptor::alloc()->init();
        descriptor->setTextureType(MTL::TextureType2D);
        descriptor->setWidth(width);
        descriptor->setHeight(height);
        descriptor->setDepth(1);
        descriptor->setMipmapLevelCount(1);
        descriptor->setArrayLength(1);
        descriptor->setSampleCount(1);
        descriptor->setPixelFormat(m_pixelFormat);
        descriptor->setUsage(defaultTextureUsage(format));
        descriptor->setStorageMode(MTL::StorageModeShared);

        m_texture = metalDevice()->newTexture(descriptor);
        descriptor->release();
        IR_ASSERT(m_texture != nullptr, "Failed to create Metal 2D texture");
    }

    ~MetalTexture2DImpl() override {
        if (m_texture != nullptr) {
            m_texture->release();
            m_texture = nullptr;
        }
    }

    uvec2 getSize() const override {
        return m_size;
    }

    std::uint32_t getHandle() const override {
        return m_handle;
    }

    void *getNativeTexture() const override {
        return m_texture;
    }

    std::uint32_t getNativePixelFormat() const override {
        return static_cast<std::uint32_t>(m_pixelFormat);
    }

    void bind(std::uint32_t unit) const override {
        bindMetalTexture(unit, m_texture);
    }

    void bindImage(
        std::uint32_t unit,
        TextureAccess,
        TextureFormat,
        int,
        bool,
        int
    ) const override {
        bindMetalImageTexture(unit, m_texture);
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
        if (data == nullptr || width <= 0 || height <= 0) {
            return;
        }
        const std::size_t bytesPerRow =
            static_cast<std::size_t>(width) * pixelSizeBytes(format, type);
        m_texture->replaceRegion(
            MTL::Region::Make2D(xoffset, yoffset, width, height),
            0,
            data,
            static_cast<NS::UInteger>(bytesPerRow)
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
        if (data == nullptr || width <= 0 || height <= 0) {
            return;
        }
        const std::size_t bytesPerRow =
            static_cast<std::size_t>(width) * pixelSizeBytes(format, type);
        m_texture->getBytes(
            data,
            static_cast<NS::UInteger>(bytesPerRow),
            MTL::Region::Make2D(xoffset, yoffset, width, height),
            0
        );
    }

    void clear(PixelDataFormat format, PixelDataType type, const void *data) override {
        const std::size_t pixelSize = pixelSizeBytes(format, type);
        std::vector<std::uint8_t> clearData(
            static_cast<std::size_t>(m_size.x) * static_cast<std::size_t>(m_size.y) * pixelSize
        );
        if (data != nullptr) {
            for (std::size_t i = 0; i < clearData.size(); i += pixelSize) {
                std::memcpy(clearData.data() + i, data, pixelSize);
            }
        } else {
            std::memset(clearData.data(), 0, clearData.size());
        }
        uploadSubImage2D(0, 0, m_size.x, m_size.y, format, type, clearData.data());
    }

    MTL::Texture *texture() const {
        return m_texture;
    }

    MTL::PixelFormat pixelFormat() const {
        return m_pixelFormat;
    }

  private:
    uvec2 m_size;
    MTL::Texture *m_texture = nullptr;
    MTL::PixelFormat m_pixelFormat = MTL::PixelFormatInvalid;
    std::uint32_t m_handle = 0;
};

class MetalTexture3DImpl final : public Texture3DImpl {
  public:
    MetalTexture3DImpl(
        unsigned int width,
        unsigned int height,
        unsigned int depth,
        TextureFormat format
    )
        : m_handle(nextMetalTextureHandle()) {
        auto *descriptor = MTL::TextureDescriptor::alloc()->init();
        descriptor->setTextureType(MTL::TextureType3D);
        descriptor->setWidth(width);
        descriptor->setHeight(height);
        descriptor->setDepth(depth);
        descriptor->setMipmapLevelCount(1);
        descriptor->setPixelFormat(toMetalTextureFormat(format));
        descriptor->setUsage(defaultTextureUsage(format));
        descriptor->setStorageMode(MTL::StorageModeShared);
        m_texture = metalDevice()->newTexture(descriptor);
        descriptor->release();
        IR_ASSERT(m_texture != nullptr, "Failed to create Metal 3D texture");
    }

    ~MetalTexture3DImpl() override {
        if (m_texture != nullptr) {
            m_texture->release();
            m_texture = nullptr;
        }
    }

    std::uint32_t getHandle() const override {
        return m_handle;
    }

    void *getNativeTexture() const {
        return m_texture;
    }

    std::uint32_t getNativePixelFormat() const {
        return static_cast<std::uint32_t>(m_texture->pixelFormat());
    }

    void bind(std::uint32_t unit) override {
        bindMetalTexture(unit, m_texture);
    }

    void uploadSubImage3D(
        int width,
        int height,
        int depth,
        PixelDataFormat format,
        PixelDataType type,
        const void *data
    ) override {
        if (data == nullptr || width <= 0 || height <= 0 || depth <= 0) {
            return;
        }
        const std::size_t bytesPerPixel = pixelSizeBytes(format, type);
        m_texture->replaceRegion(
            MTL::Region(0, 0, 0, width, height, depth),
            0,
            0,
            data,
            static_cast<NS::UInteger>(width * bytesPerPixel),
            static_cast<NS::UInteger>(width * height * bytesPerPixel)
        );
    }

  private:
    MTL::Texture *m_texture = nullptr;
    std::uint32_t m_handle = 0;
};

std::unique_ptr<Texture2DImpl> createTexture2DImpl(
    TextureKind,
    unsigned int width,
    unsigned int height,
    TextureFormat internalFormat,
    TextureWrap,
    TextureFilter,
    int
) {
    return std::make_unique<MetalTexture2DImpl>(width, height, internalFormat);
}

std::unique_ptr<Texture3DImpl> createTexture3DImpl(
    TextureKind,
    unsigned int width,
    unsigned int height,
    unsigned int depth,
    TextureFormat internalFormat,
    TextureWrap,
    TextureFilter
) {
    return std::make_unique<MetalTexture3DImpl>(width, height, depth, internalFormat);
}

} // namespace IRRender

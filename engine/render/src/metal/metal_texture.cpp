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
        case TextureFormat::RGBA16F:
            return MTL::PixelFormatRGBA16Float;
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

std::size_t metalPixelFormatBytes(MTL::PixelFormat format) {
    switch (format) {
        case MTL::PixelFormatRGBA8Unorm: return 4;
        case MTL::PixelFormatRGBA16Float: return 8;
        case MTL::PixelFormatRGBA32Float: return 16;
        case MTL::PixelFormatR32Sint: return 4;
        case MTL::PixelFormatRG32Uint: return 8;
        case MTL::PixelFormatDepth32Float_Stencil8: return 8;
        default: return 4;
    }
}

// IEEE-754 binary32 → binary16 (round-to-nearest-even, with denormal flush).
// Inlined here because Metal's `replaceRegion` does not format-convert source
// bytes — RGBA8 source data uploaded into an RGBA16F texture would otherwise
// be reinterpreted as raw bits, corrupting the canvas.
inline std::uint16_t f32ToF16(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    std::int32_t exp = static_cast<std::int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
    std::uint32_t mant = bits & 0x7FFFFFu;
    if (exp >= 31) {
        const bool isNan = ((bits & 0x7FFFFFFFu) > 0x7F800000u);
        return static_cast<std::uint16_t>(sign | 0x7C00u | (isNan ? 0x200u : 0u));
    }
    if (exp <= 0) {
        if (exp < -10) {
            return static_cast<std::uint16_t>(sign);
        }
        mant |= 0x800000u;
        const std::uint32_t shift = static_cast<std::uint32_t>(14 - exp);
        return static_cast<std::uint16_t>(sign | (mant >> shift));
    }
    return static_cast<std::uint16_t>(
        sign | (static_cast<std::uint32_t>(exp) << 10) | (mant >> 13)
    );
}

// Source `data` is `width × height` pixels in `(srcFormat, srcType)`. Convert
// to the byte layout the texture's MTL::PixelFormat expects, returning the
// converted buffer. If no conversion is needed the returned vector is empty
// and callers should upload `data` directly.
std::vector<std::uint8_t> convertToTextureFormat(
    MTL::PixelFormat texturePixelFormat,
    PixelDataFormat srcFormat,
    PixelDataType srcType,
    int width,
    int height,
    const void *data
) {
    const bool isHdrTarget =
        (texturePixelFormat == MTL::PixelFormatRGBA16Float);
    const bool isRgbaSource = (srcFormat == PixelDataFormat::RGBA);
    if (!isHdrTarget || !isRgbaSource) {
        return {};
    }
    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<std::uint8_t> out(pixelCount * 8, 0);  // 4 components × fp16
    auto *dst = reinterpret_cast<std::uint16_t *>(out.data());
    if (srcType == PixelDataType::UNSIGNED_BYTE) {
        const auto *src = static_cast<const std::uint8_t *>(data);
        for (std::size_t i = 0; i < pixelCount; ++i) {
            dst[i * 4 + 0] = f32ToF16(static_cast<float>(src[i * 4 + 0]) / 255.0f);
            dst[i * 4 + 1] = f32ToF16(static_cast<float>(src[i * 4 + 1]) / 255.0f);
            dst[i * 4 + 2] = f32ToF16(static_cast<float>(src[i * 4 + 2]) / 255.0f);
            dst[i * 4 + 3] = f32ToF16(static_cast<float>(src[i * 4 + 3]) / 255.0f);
        }
        return out;
    }
    if (srcType == PixelDataType::FLOAT32) {
        const auto *src = static_cast<const float *>(data);
        for (std::size_t i = 0; i < pixelCount * 4; ++i) {
            dst[i] = f32ToF16(src[i]);
        }
        return out;
    }
    return {};
}

} // namespace

class MetalTexture2DImpl final : public Texture2DImpl {
  public:
    MetalTexture2DImpl(unsigned int width, unsigned int height, TextureFormat format)
        : m_size(width, height)
        , m_pixelFormat(toMetalTextureFormat(format)) {
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
        releaseImageAtomicScratchBuffer(m_texture);
        if (m_texture != nullptr) {
            m_texture->release();
            m_texture = nullptr;
        }
    }

    uvec2 getSize() const override {
        return m_size;
    }

    std::uint32_t getHandle() const override {
        return 0;
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
        // R32I distance images participate in atomic image-min operations
        // through a sibling scratch buffer; see metal_runtime.hpp.
        if (m_pixelFormat == MTL::PixelFormatR32Sint) {
            setCurrentImageAtomicScratch(ensureImageAtomicScratchBuffer(m_texture));
        }
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
        auto converted =
            convertToTextureFormat(m_pixelFormat, format, type, width, height, data);
        const void *uploadData = converted.empty() ? data : converted.data();
        const std::size_t srcPixelSize =
            converted.empty() ? pixelSizeBytes(format, type)
                              : metalPixelFormatBytes(m_pixelFormat);
        const std::size_t bytesPerRow =
            static_cast<std::size_t>(width) * srcPixelSize;
        m_texture->replaceRegion(
            MTL::Region::Make2D(xoffset, yoffset, width, height),
            0,
            uploadData,
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
        const bool needsConvert =
            (m_pixelFormat == MTL::PixelFormatRGBA16Float &&
             format == PixelDataFormat::RGBA &&
             type == PixelDataType::UNSIGNED_BYTE);
        if (!needsConvert) {
            const std::size_t bytesPerRow =
                static_cast<std::size_t>(width) * pixelSizeBytes(format, type);
            m_texture->getBytes(
                data,
                static_cast<NS::UInteger>(bytesPerRow),
                MTL::Region::Make2D(xoffset, yoffset, width, height),
                0
            );
            return;
        }
        // RGBA16F → u8: snapshot into half-floats, tonemap-clamp to LDR, scale to u8.
        // saveToFile is the only consumer that hits this branch; HDR values >1.0 are
        // clipped (same loss the PNG encoder would impose anyway).
        const std::size_t pixelCount =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        std::vector<std::uint16_t> halfBuf(pixelCount * 4);
        m_texture->getBytes(
            halfBuf.data(),
            static_cast<NS::UInteger>(width * 8),
            MTL::Region::Make2D(xoffset, yoffset, width, height),
            0
        );
        auto *out = static_cast<std::uint8_t *>(data);
        for (std::size_t i = 0; i < pixelCount * 4; ++i) {
            const std::uint16_t h = halfBuf[i];
            const std::uint32_t sign = (std::uint32_t(h) & 0x8000u) << 16;
            std::int32_t exp = static_cast<std::int32_t>((h >> 10) & 0x1Fu);
            std::uint32_t mant = h & 0x3FFu;
            std::uint32_t fbits = 0;
            if (exp == 0 && mant == 0) {
                fbits = sign;
            } else if (exp == 31) {
                fbits = sign | 0x7F800000u | (mant << 13);
            } else if (exp == 0) {
                while ((mant & 0x400u) == 0) {
                    mant <<= 1;
                    --exp;
                }
                ++exp;
                mant &= 0x3FFu;
                fbits = sign |
                        (static_cast<std::uint32_t>(exp + 127 - 15) << 23) |
                        (mant << 13);
            } else {
                fbits = sign |
                        (static_cast<std::uint32_t>(exp + 127 - 15) << 23) |
                        (mant << 13);
            }
            float fv = 0.0f;
            std::memcpy(&fv, &fbits, sizeof(fv));
            const float clamped = fv < 0.0f ? 0.0f : (fv > 1.0f ? 1.0f : fv);
            out[i] = static_cast<std::uint8_t>(clamped * 255.0f + 0.5f);
        }
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
};

class MetalTexture3DImpl final : public Texture3DImpl {
  public:
    MetalTexture3DImpl(
        unsigned int width,
        unsigned int height,
        unsigned int depth,
        TextureFormat format
    ) {
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
        return 0;
    }

    void *getNativeTexture() const override {
        return m_texture;
    }

    std::uint32_t getNativePixelFormat() const override {
        return static_cast<std::uint32_t>(m_texture->pixelFormat());
    }

    void bind(std::uint32_t unit) override {
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
        // Metal flattens sample/image bindings into one slot space; the
        // shader-side `texture3d<…, access::*>` declaration carries the
        // R/W intent, so we just route through the image-binding table
        // (mirrors `MetalTexture2DImpl::bindImage`).
        bindMetalImageTexture(unit, m_texture);
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

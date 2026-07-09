#include <irreden/render/texture.hpp>
#include <irreden/render/metal/metal_runtime.hpp>
#include <irreden/ir_profile.hpp>

#include <array>
#include <cstring>

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
        case TextureFormat::R16UI:
            return MTL::PixelFormatR16Uint;
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
            case PixelDataFormat::DEPTH_COMPONENT:
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
        removeClearSourceBuffer(m_texture);
        // Drop any sticky sampler/image bind slot that still references this
        // handle so a later dispatch's bind pass can't re-bind the freed
        // texture (#1961).
        untrackMetalTexture(m_texture);
        if (m_texture != nullptr) {
            m_texture->release();
            m_texture = nullptr;
        }
        if (m_clearSourceBuf != nullptr) {
            m_clearSourceBuf->release();
            m_clearSourceBuf = nullptr;
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
        const std::size_t bytesPerRow =
            static_cast<std::size_t>(width) * pixelSizeBytes(format, type);
        const std::size_t totalSize = bytesPerRow * static_cast<std::size_t>(height);

        auto *commandBuffer = metalCommandBuffer();
        if (commandBuffer == nullptr) {
            // No frame command buffer (startup / one-off init): no GPU work is
            // queued against this texture, so a direct CPU write is safe and
            // skips the transient staging buffer.
            m_texture->replaceRegion(
                MTL::Region::Make2D(xoffset, yoffset, width, height),
                0,
                data,
                static_cast<NS::UInteger>(bytesPerRow)
            );
            return;
        }

        // A frame command buffer is open. replaceRegion is an immediate CPU
        // write to shared storage and is NOT ordered against the GPU encoders
        // already queued on this texture this frame — most importantly the
        // deferred clear blit (Texture2D::clear / C_TriangleCanvasTextures::clear)
        // that executes at commit time and would clobber a CPU write made now.
        // The GUI canvas hit this every frame: TEXT_TO_TRIXEL queues the clear
        // blit then writes glyphs via a compute imageStore (GPU, survives the
        // clear), while the WIDGET_RENDER_* systems draw panels/borders/labels
        // via subImage2D — a CPU replaceRegion was erased by the queued clear, so
        // widgets rendered invisible while text composited fine (#1436). Stage the
        // upload and blit copyFromBuffer so it lands in encoder order after the
        // clear/compute, matching OpenGL's submission-ordered subImage2D.
        // Per-call transient staging buffer. At current call frequency (a handful
        // of widget panels/borders/labels per frame, fog-of-war dirty-gated) the
        // per-frame allocation cost is negligible. If a future system drives
        // high-rate per-frame subImage2D on Metal, replace with a per-texture
        // reusable staging buffer mirroring m_clearSourceBuf's change-detected
        // reuse pattern in clear().
        MTL::Buffer *staging = metalDevice()->newBuffer(
            data,
            static_cast<NS::UInteger>(totalSize),
            MTL::ResourceStorageModeShared
        );
        IR_ASSERT(staging != nullptr, "Failed to create Metal subImage2D staging buffer");

        auto *blit = commandBuffer->blitCommandEncoder();
        blit->copyFromBuffer(
            staging,
            0,
            static_cast<NS::UInteger>(bytesPerRow),
            static_cast<NS::UInteger>(totalSize),
            MTL::Size::Make(width, height, 1),
            m_texture,
            0,
            0,
            MTL::Origin::Make(xoffset, yoffset, 0)
        );
        blit->endEncoding();

        // The staging buffer must outlive the GPU read. releaseDeferredMetalBuffers()
        // runs only after the frame's waitUntilCompleted(), so defer its release.
        deferReleaseMetalBuffer(staging);
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
        const std::size_t totalSize =
            static_cast<std::size_t>(m_size.x) * static_cast<std::size_t>(m_size.y) * pixelSize;

        // Lazy-allocate a persistent SharedMode source buffer (once per texture).
        if (m_clearSourceBuf == nullptr) {
            m_clearSourceBuf = metalDevice()->newBuffer(
                totalSize, MTL::ResourceStorageModeShared
            );
            IR_ASSERT(m_clearSourceBuf != nullptr, "Failed to create Metal texture clear buffer");
            m_clearPixelSize = 0;  // force fill on first use
        }

        // Refill the source buffer only when the per-pixel clear value changes.
        // Constant per-frame clears (black, max-distance, zero) never trigger a refill.
        IR_ASSERT(pixelSize <= m_clearPixelData.size(), "pixel size exceeds change-detection cache — widen m_clearPixelData");
        const bool nullClear = (data == nullptr);
        const bool patternChanged =
            (pixelSize != m_clearPixelSize) ||
            (nullClear != m_clearDataWasNull) ||
            (!nullClear && std::memcmp(m_clearPixelData.data(), data, pixelSize) != 0);

        if (patternChanged) {
            auto *bytes = static_cast<std::uint8_t *>(m_clearSourceBuf->contents());
            if (!nullClear) {
                for (std::size_t i = 0; i < totalSize; i += pixelSize) {
                    std::memcpy(bytes + i, data, pixelSize);
                }
                std::memcpy(m_clearPixelData.data(), data, pixelSize);
            } else {
                std::memset(bytes, 0, totalSize);
            }
            m_clearPixelSize = pixelSize;
            m_clearDataWasNull = nullClear;
        }

        auto *commandBuffer = metalCommandBuffer();
        if (commandBuffer != nullptr) {
            // GPU-side blit: no per-frame allocation, no replaceRegion stall.
            auto *blit = commandBuffer->blitCommandEncoder();
            blit->copyFromBuffer(
                m_clearSourceBuf,
                0,
                static_cast<NS::UInteger>(m_size.x * pixelSize),
                totalSize,
                MTL::Size::Make(m_size.x, m_size.y, 1),
                m_texture,
                0,
                0,
                MTL::Origin::Make(0, 0, 0)
            );
            blit->endEncoding();
        } else {
            // No command buffer (e.g. during startup init): fall back to replaceRegion.
            uploadSubImage2D(
                0, 0, m_size.x, m_size.y, format, type,
                m_clearSourceBuf->contents()
            );
        }
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
    MTL::Buffer *m_clearSourceBuf = nullptr;
    std::array<std::uint8_t, 16> m_clearPixelData{};
    std::size_t m_clearPixelSize = 0;
    bool m_clearDataWasNull = true;
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
        // Drop any sticky bind slot referencing this handle (#1961).
        untrackMetalTexture(m_texture);
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
        // NOTE: unlike uploadSubImage2D, this path does not check metalCommandBuffer()
        // and always uses a direct CPU replaceRegion. 3D textures (light-volume RGBA8
        // volumes) are seeded once at init and never cleared via GPU blit in the same
        // frame as a CPU write, so the deferred-clear ordering hazard (#1436) cannot
        // occur today. If a future system clears a 3D texture per-frame via the command
        // buffer and writes it via uploadSubImage3D in the same tick, apply the same
        // commandBuffer-guard + staging-blit pattern used in uploadSubImage2D.
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

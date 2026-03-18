#include <irreden/render/framebuffer.hpp>
#include <irreden/render/metal/metal_runtime.hpp>
#include <irreden/render/vao.hpp>

namespace IRRender {

namespace {

MTL::VertexFormat toMetalVertexFormat(VertexAttributeDataType type, int size) {
    if (type == VertexAttributeDataType::FLOAT32) {
        switch (size) {
            case 2:
                return MTL::VertexFormatFloat2;
            case 3:
                return MTL::VertexFormatFloat3;
            case 4:
                return MTL::VertexFormatFloat4;
            default:
                return MTL::VertexFormatFloat;
        }
    }
    return MTL::VertexFormatInvalid;
}

std::size_t vertexAttributeSizeBytes(const VertexArrayAttribute &attribute) {
    return static_cast<std::size_t>(attribute.size_) * sizeof(float);
}

} // namespace

class MetalFramebufferImpl final : public FramebufferImpl {
  public:
    MetalFramebufferImpl(const Texture2D &textureColor, const Texture2D &textureDepth)
        : m_colorTexture(static_cast<MTL::Texture *>(textureColor.getNativeTexture()))
        , m_depthTexture(static_cast<MTL::Texture *>(textureDepth.getNativeTexture()))
        , m_colorPixelFormat(
              static_cast<MTL::PixelFormat>(textureColor.getNativePixelFormat())
          )
        , m_depthPixelFormat(
              static_cast<MTL::PixelFormat>(textureDepth.getNativePixelFormat())
          ) {}

    void bind() const override {
        bindMetalFramebufferRenderTarget(
            m_colorTexture,
            m_depthTexture,
            m_colorPixelFormat,
            m_depthPixelFormat
        );
    }

    void unbind() override {
        bindMetalDefaultRenderTarget();
    }

    void clear() const override {
        requestMetalRenderTargetClear();
    }

  private:
    MTL::Texture *m_colorTexture = nullptr;
    MTL::Texture *m_depthTexture = nullptr;
    MTL::PixelFormat m_colorPixelFormat = MTL::PixelFormatInvalid;
    MTL::PixelFormat m_depthPixelFormat = MTL::PixelFormatInvalid;
};

class MetalVertexLayoutImpl final : public VertexLayoutImpl {
  public:
    MetalVertexLayoutImpl(
        std::uint32_t vertexBufferHandle,
        std::uint32_t indexBufferHandle,
        unsigned int numAttributes,
        const VertexArrayAttribute *attributes
    ) {
        m_vertexBuffer = lookupMetalBufferHandle(vertexBufferHandle);
        m_indexBuffer = lookupMetalBufferHandle(indexBufferHandle);

        auto *vertexDescriptor = MTL::VertexDescriptor::alloc()->init();
        std::size_t stride = 0;
        for (unsigned int i = 0; i < numAttributes; ++i) {
            const auto attributeSize = vertexAttributeSizeBytes(attributes[i]);
            auto *attribute = vertexDescriptor->attributes()->object(i);
            attribute->setFormat(toMetalVertexFormat(attributes[i].type_, attributes[i].size_));
            attribute->setOffset(static_cast<NS::UInteger>(stride));
            attribute->setBufferIndex(0);
            stride += attributeSize;
        }
        vertexDescriptor->layouts()->object(0)->setStride(static_cast<NS::UInteger>(stride));
        m_vertexDescriptor = vertexDescriptor;
    }

    ~MetalVertexLayoutImpl() override {
        if (m_vertexDescriptor != nullptr) {
            m_vertexDescriptor->release();
            m_vertexDescriptor = nullptr;
        }
    }

    void bind() const override {
        setActiveMetalVertexLayout(m_vertexBuffer, m_indexBuffer, m_vertexDescriptor);
    }

  private:
    MTL::Buffer *m_vertexBuffer = nullptr;
    MTL::Buffer *m_indexBuffer = nullptr;
    MTL::VertexDescriptor *m_vertexDescriptor = nullptr;
};

std::unique_ptr<FramebufferImpl> createFramebufferImpl(
    const Texture2D &textureColor,
    const Texture2D &textureDepth
) {
    return std::make_unique<MetalFramebufferImpl>(textureColor, textureDepth);
}

std::unique_ptr<VertexLayoutImpl> createVertexLayoutImpl(
    std::uint32_t vertexBufferHandle,
    std::uint32_t indexBufferHandle,
    unsigned int numAttributes,
    const VertexArrayAttribute *attributes
) {
    return std::make_unique<MetalVertexLayoutImpl>(
        vertexBufferHandle,
        indexBufferHandle,
        numAttributes,
        attributes
    );
}

} // namespace IRRender

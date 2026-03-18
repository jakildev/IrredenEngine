#include <irreden/render/metal/metal_render_impl.hpp>
#include <irreden/render/metal/metal_cocoa_bridge.hpp>
#include <irreden/render/metal/metal_runtime.hpp>
#include <irreden/render/render_device.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace IRRender {

namespace {

constexpr std::uint32_t kMaxMetalBindings = 32;

MTL::PrimitiveType toMetalPrimitiveType(DrawMode drawMode) {
    switch (drawMode) {
        case DrawMode::TRIANGLES:
            return MTL::PrimitiveTypeTriangle;
        case DrawMode::LINES:
            return MTL::PrimitiveTypeLine;
    }
    return MTL::PrimitiveTypeTriangle;
}

MTL::IndexType toMetalIndexType(IndexType indexType) {
    switch (indexType) {
        case IndexType::UNSIGNED_SHORT:
            return MTL::IndexTypeUInt16;
    }
    return MTL::IndexTypeUInt16;
}

void bindRenderResources(MTL::RenderCommandEncoder *encoder) {
    for (std::uint32_t i = 0; i < kMaxMetalBindings; ++i) {
        const auto &uniform = boundMetalBuffer(BufferTarget::UNIFORM, i);
        if (uniform.buffer_ != nullptr) {
            encoder->setVertexBuffer(uniform.buffer_, uniform.offset_, i);
            encoder->setFragmentBuffer(uniform.buffer_, uniform.offset_, i);
        }

        const auto &storage = boundMetalBuffer(BufferTarget::SHADER_STORAGE, i);
        if (storage.buffer_ != nullptr) {
            encoder->setVertexBuffer(storage.buffer_, storage.offset_, i);
            encoder->setFragmentBuffer(storage.buffer_, storage.offset_, i);
        }

        if (MTL::Texture *texture = boundMetalTexture(i); texture != nullptr) {
            encoder->setVertexTexture(texture, i);
            encoder->setFragmentTexture(texture, i);
        }
    }
}

void bindComputeResources(MTL::ComputeCommandEncoder *encoder) {
    for (std::uint32_t i = 0; i < kMaxMetalBindings; ++i) {
        const auto &uniform = boundMetalBuffer(BufferTarget::UNIFORM, i);
        if (uniform.buffer_ != nullptr) {
            encoder->setBuffer(uniform.buffer_, uniform.offset_, i);
        }

        const auto &storage = boundMetalBuffer(BufferTarget::SHADER_STORAGE, i);
        if (storage.buffer_ != nullptr) {
            encoder->setBuffer(storage.buffer_, storage.offset_, i);
        }

        if (MTL::Texture *texture = boundMetalTexture(i); texture != nullptr) {
            encoder->setTexture(texture, i);
        }
        if (MTL::Texture *imageTexture = boundMetalImageTexture(i); imageTexture != nullptr) {
            encoder->setTexture(imageTexture, i);
        }
    }
}

MTL::RenderCommandEncoder *createRenderEncoder() {
    auto *commandBuffer = metalCommandBuffer();
    if (commandBuffer == nullptr) {
        return nullptr;
    }

    MTL::Texture *colorTexture = nullptr;
    MTL::Texture *depthTexture = nullptr;
    MTL::PixelFormat colorPixelFormat = metalCurrentColorPixelFormat();
    MTL::PixelFormat depthPixelFormat = metalCurrentDepthPixelFormat();

    if (metalUsesDefaultRenderTarget()) {
        auto *drawable = metalDrawable();
        if (drawable == nullptr) {
            return nullptr;
        }
        colorTexture = drawable->texture();
        colorPixelFormat = colorTexture->pixelFormat();
    } else {
        colorTexture = metalCurrentColorTexture();
        depthTexture = metalCurrentDepthTexture();
    }

    if (colorTexture == nullptr) {
        return nullptr;
    }

    const bool clearTarget = consumeMetalRenderTargetClear();

    auto *renderPassDescriptor = MTL::RenderPassDescriptor::alloc()->init();
    auto *colorAttachment = renderPassDescriptor->colorAttachments()->object(0);
    colorAttachment->setTexture(colorTexture);
    colorAttachment->setLoadAction(clearTarget ? MTL::LoadActionClear : MTL::LoadActionLoad);
    colorAttachment->setStoreAction(MTL::StoreActionStore);
    if (metalUsesDefaultRenderTarget()) {
        colorAttachment->setClearColor(MTL::ClearColor(0.15, 0.0, 0.25, 1.0));
    } else {
        colorAttachment->setClearColor(MTL::ClearColor(0.0, 0.0, 0.0, 1.0));
    }

    if (depthTexture != nullptr) {
        auto *depthAttachment = renderPassDescriptor->depthAttachment();
        depthAttachment->setTexture(depthTexture);
        depthAttachment->setLoadAction(clearTarget ? MTL::LoadActionClear : MTL::LoadActionLoad);
        depthAttachment->setStoreAction(MTL::StoreActionStore);
        depthAttachment->setClearDepth(1.0);
    }

    auto *encoder = commandBuffer->renderCommandEncoder(renderPassDescriptor);
    renderPassDescriptor->release();
    const double viewportWidth = static_cast<double>(colorTexture->width());
    const double viewportHeight = static_cast<double>(colorTexture->height());
    encoder->setViewport(MTL::Viewport(0.0, 0.0, viewportWidth, viewportHeight, 0.0, 1.0));
    encoder->setScissorRect(MTL::ScissorRect(
        0,
        0,
        static_cast<NS::UInteger>(colorTexture->width()),
        static_cast<NS::UInteger>(colorTexture->height())
    ));
    return encoder;
}

class MetalRenderDevice final : public RenderDevice {
  public:
    void init(MTL::Device *device, CA::MetalLayer *layer) {
initializeMetalRuntime(device, layer);
bindMetalDefaultRenderTarget();
    }

    void shutdown() {
shutdownMetalRuntime();
    }

    void beginFrame() override {
        auto *drawable = metalLayer()->nextDrawable();
        if (drawable == nullptr) {
setMetalDrawable(nullptr);
setMetalCommandBuffer(nullptr);
            return;
        }
setMetalDrawable(drawable);
setMetalCommandBuffer(metalCommandQueue()->commandBuffer());
    }

    void present() override {
        auto *commandBuffer = metalCommandBuffer();
        if (commandBuffer == nullptr) {
            return;
        }
        if (auto *drawable = metalDrawable(); drawable != nullptr) {
            commandBuffer->presentDrawable(drawable);
        }
        commandBuffer->commit();
        commandBuffer->waitUntilCompleted();
setMetalDrawable(nullptr);
setMetalCommandBuffer(nullptr);
    }

    void clearDefaultFramebuffer() override {
bindMetalDefaultRenderTarget();
requestMetalRenderTargetClear();
    }

    void bindDefaultFramebuffer() override {
bindMetalDefaultRenderTarget();
    }

    bool readDefaultFramebuffer(int x, int y, int width, int height, void *rgbaData) override {
        if (rgbaData == nullptr || width <= 0 || height <= 0) {
            return false;
        }

        auto *drawable = metalDrawable();
        if (drawable == nullptr) {
            return false;
        }

        MTL::Texture *texture = drawable->texture();
        if (texture == nullptr) {
            return false;
        }

        if (x < 0 || y < 0 || x + width > static_cast<int>(texture->width()) ||
            y + height > static_cast<int>(texture->height())) {
            return false;
        }

        const std::size_t rowBytes = static_cast<std::size_t>(width) * 4U;
        std::vector<std::uint8_t> bgraData(static_cast<std::size_t>(height) * rowBytes);
        texture->getBytes(
            bgraData.data(),
            static_cast<NS::UInteger>(rowBytes),
            MTL::Region::Make2D(x, y, width, height),
            0
        );

        auto *rgbaPixels = static_cast<std::uint8_t *>(rgbaData);
        const bool needsBgraToRgbaSwizzle = texture->pixelFormat() == MTL::PixelFormatBGRA8Unorm ||
                                            texture->pixelFormat() == MTL::PixelFormatBGRA8Unorm_sRGB;

        if (!needsBgraToRgbaSwizzle) {
            std::memcpy(rgbaPixels, bgraData.data(), bgraData.size());
            return true;
        }

        for (std::size_t i = 0; i < bgraData.size(); i += 4U) {
            rgbaPixels[i + 0] = bgraData[i + 2];
            rgbaPixels[i + 1] = bgraData[i + 1];
            rgbaPixels[i + 2] = bgraData[i + 0];
            rgbaPixels[i + 3] = bgraData[i + 3];
        }

        return true;
    }

    void dispatchCompute(std::uint32_t x, std::uint32_t y, std::uint32_t z) override {
        auto *commandBuffer = metalCommandBuffer();
        auto *pipeline = activeMetalPipeline();
        if (commandBuffer == nullptr || pipeline == nullptr || !pipeline->isComputePipeline()) {
            return;
        }

        auto *encoder = commandBuffer->computeCommandEncoder();
        encoder->setComputePipelineState(pipeline->getComputePipelineState());
        bindComputeResources(encoder);
        encoder->dispatchThreadgroups(
            MTL::Size(x, y, z),
            pipeline->getThreadsPerThreadgroup()
        );
        encoder->endEncoding();
    }

    void memoryBarrier(BarrierType) override {}
    void drawElements(DrawMode drawMode, int count, IndexType indexType) override {
        auto *pipeline = activeMetalPipeline();
        const auto &layout = activeMetalVertexLayout();
        if (pipeline == nullptr || pipeline->isComputePipeline() || layout.indexBuffer_ == nullptr) {
            return;
        }

        auto *encoder = createRenderEncoder();
        if (encoder == nullptr) {
            return;
        }

        auto *pipelineState = pipeline->getRenderPipelineState(
metalCurrentColorPixelFormat(),
metalCurrentDepthPixelFormat(),
            layout.vertexDescriptor_
        );
        IR_ASSERT(pipelineState != nullptr, "Failed to get Metal render pipeline state");
        encoder->setRenderPipelineState(pipelineState);
        if (metalCurrentDepthTexture() != nullptr) {
            encoder->setDepthStencilState(metalDepthStencilState());
        }
        bindRenderResources(encoder);
        encoder->setVertexBuffer(layout.vertexBuffer_, 0, 0);
        encoder->drawIndexedPrimitives(
            toMetalPrimitiveType(drawMode),
            static_cast<NS::UInteger>(count),
            toMetalIndexType(indexType),
            layout.indexBuffer_,
            0
        );
        encoder->endEncoding();
    }

    void drawArrays(DrawMode drawMode, int first, int count) override {
        auto *pipeline = activeMetalPipeline();
        const auto &layout = activeMetalVertexLayout();
        if (pipeline == nullptr || pipeline->isComputePipeline()) {
            return;
        }

        auto *encoder = createRenderEncoder();
        if (encoder == nullptr) {
            return;
        }

        auto *pipelineState = pipeline->getRenderPipelineState(
metalCurrentColorPixelFormat(),
metalCurrentDepthPixelFormat(),
            layout.vertexDescriptor_
        );
        IR_ASSERT(pipelineState != nullptr, "Failed to get Metal render pipeline state");
        encoder->setRenderPipelineState(pipelineState);
        if (metalCurrentDepthTexture() != nullptr) {
            encoder->setDepthStencilState(metalDepthStencilState());
        }
        bindRenderResources(encoder);
        encoder->setVertexBuffer(layout.vertexBuffer_, 0, 0);
        encoder->drawPrimitives(
            toMetalPrimitiveType(drawMode),
            static_cast<NS::UInteger>(first),
            static_cast<NS::UInteger>(count)
        );
        encoder->endEncoding();
    }

    void setPolygonMode(PolygonMode) override {}

    void enableBlending() override {}
    void disableBlending() override {}
    void setDepthTest(bool) override {}
    void setDepthWrite(bool) override {}
};

MetalRenderDevice g_metalRenderDevice;
} // namespace

std::unique_ptr<RenderImpl> createRenderer() {
    return std::make_unique<MetalRenderImpl>();
}

MetalRenderImpl::MetalRenderImpl()
    : m_device{MTL::CreateSystemDefaultDevice()} {
    IRE_LOG_INFO("Initializing Metal render implementation.");
setMetalBootstrapDevice(m_device);
}

MetalRenderImpl::~MetalRenderImpl() {
    g_metalRenderDevice.shutdown();
    if (m_device != nullptr) {
        m_device->release();
        m_device = nullptr;
    }
}

void MetalRenderImpl::init() {
    IR_ASSERT(m_device != nullptr, "Failed to create Metal device.");

    GLFWwindow *rawWindow = IRWindow::getWindow().getRawWindow();
    void *layerPtr = createMetalLayerForWindow(rawWindow, m_device);
    IR_ASSERT(layerPtr != nullptr, "Failed to create CAMetalLayer");
    m_layer = reinterpret_cast<CA::MetalLayer *>(layerPtr);

    g_metalRenderDevice.init(m_device, m_layer);
    setDevice(&g_metalRenderDevice);

    IRWindow::getWindow().setCallbackFramebufferSize(metalCallback_framebuffer_size);
    IRE_LOG_INFO("Metal surface attached to window.");
}

void MetalRenderImpl::printInfo() {
    if (m_device == nullptr) {
        IRE_LOG_WARN("Metal device is not initialized.");
        return;
    }
    auto *deviceName = m_device->name();
    IRE_LOG_INFO(
        "Metal device: {}",
        deviceName != nullptr ? deviceName->utf8String() : "<unknown>"
    );
}

void metalCallback_framebuffer_size(GLFWwindow *, int width, int height) {
resizeMetalDrawable(width, height);
    IRE_LOG_INFO("Resized Metal viewport to {}x{}", width, height);
}

} // namespace IRRender

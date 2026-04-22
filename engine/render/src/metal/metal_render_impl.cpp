#include <irreden/render/metal/metal_render_impl.hpp>
#include <irreden/render/buffer.hpp>
#include <irreden/render/metal/metal_cocoa_bridge.hpp>
#include <irreden/render/metal/metal_runtime.hpp>
#include <irreden/render/render_device.hpp>
#include <irreden/render/texture.hpp>

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

    // Image atomic scratch buffer (mirrors the canvas distance R32I texture).
    // See metal_runtime.hpp for the rationale; binding slot is fixed.
    if (MTL::Buffer *scratch = currentImageAtomicScratch(); scratch != nullptr) {
        encoder->setBuffer(scratch, 0, kMetalImageAtomicScratchSlot);
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
        // Now that the GPU has finished consuming any encoders that
        // captured orphaned buffers, it is safe to release them.
        releaseDeferredMetalBuffers();
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

        // World::render() calls videoManager.render() (which lands here for
        // screenshots) BEFORE presentFrame, so the current command buffer
        // still has the entire frame's render work merely encoded — the GPU
        // has not executed it yet. Reading texture->getBytes() at this point
        // would return stale content from a previous frame, producing an
        // off-by-one screenshot. Flush the encoded work synchronously here
        // and start a fresh command buffer so present() can still encode the
        // drawable presentation on top.
        if (auto *commandBuffer = metalCommandBuffer(); commandBuffer != nullptr) {
            commandBuffer->commit();
            commandBuffer->waitUntilCompleted();
            releaseDeferredMetalBuffers();
            setMetalCommandBuffer(metalCommandQueue()->commandBuffer());
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

    void memoryBarrier(BarrierType) override {
        // Metal command-encoder boundaries (one encoder per dispatch in this
        // backend) act as implicit barriers between dispatches that touch
        // shared resources, so an explicit barrier is unnecessary here.
    }

    void dispatchComputeIndirect(const Buffer *indirectBuffer, std::ptrdiff_t offset) override {
        auto *commandBuffer = metalCommandBuffer();
        auto *pipeline = activeMetalPipeline();
        if (commandBuffer == nullptr || pipeline == nullptr || !pipeline->isComputePipeline()) {
            return;
        }
        if (indirectBuffer == nullptr) {
            return;
        }
        auto *mtlIndirectBuffer = static_cast<MTL::Buffer *>(indirectBuffer->getNativeBuffer());
        if (mtlIndirectBuffer == nullptr) {
            return;
        }

        auto *encoder = commandBuffer->computeCommandEncoder();
        encoder->setComputePipelineState(pipeline->getComputePipelineState());
        bindComputeResources(encoder);
        encoder->dispatchThreadgroups(
            mtlIndirectBuffer,
            static_cast<NS::UInteger>(offset),
            pipeline->getThreadsPerThreadgroup()
        );
        encoder->endEncoding();
    }
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
            encoder->setDepthStencilState(currentMetalDepthStencilState());
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

    void drawElementsInstanced(DrawMode drawMode, int count, IndexType indexType, int instanceCount) override {
        if (instanceCount <= 0) {
            return;
        }
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
            encoder->setDepthStencilState(currentMetalDepthStencilState());
        }
        bindRenderResources(encoder);
        encoder->setVertexBuffer(layout.vertexBuffer_, 0, 0);
        encoder->drawIndexedPrimitives(
            toMetalPrimitiveType(drawMode),
            static_cast<NS::UInteger>(count),
            toMetalIndexType(indexType),
            layout.indexBuffer_,
            0,
            static_cast<NS::UInteger>(instanceCount)
        );
        encoder->endEncoding();
    }

    void copyImageSubData(
        std::uint32_t, int, int, int, int,
        std::uint32_t, int, int, int, int,
        int, int, int
    ) override {
        // Metal texture copy - stub.
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
            encoder->setDepthStencilState(currentMetalDepthStencilState());
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
    void setDepthTest(bool enabled) override {
setMetalDepthTestEnabled(enabled);
    }
    void setDepthWrite(bool enabled) override {
setMetalDepthWriteEnabled(enabled);
    }
    void clearTexImage(const Texture2D *textureWrapper, int level, const void *data) override {
        if (textureWrapper == nullptr) {
            return;
        }
        auto *texture = static_cast<MTL::Texture *>(textureWrapper->getNativeTexture());
        if (texture == nullptr) {
            return;
        }
        const NS::UInteger width = texture->width();
        const NS::UInteger height = texture->height();
        if (width == 0 || height == 0) {
            return;
        }
        // Pixel size derived from format. For the formats we currently
        // create through the engine (RGBA8, R32I, RG32UI, RGBA32F) the
        // bytes-per-pixel is uniquely determined.
        std::size_t bytesPerPixel = 4;
        const auto pixelFormat = texture->pixelFormat();
        if (pixelFormat == MTL::PixelFormatRG32Uint) {
            bytesPerPixel = 8;
        } else if (pixelFormat == MTL::PixelFormatRGBA32Float) {
            bytesPerPixel = 16;
        }

        std::vector<std::uint8_t> clearData(
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * bytesPerPixel
        );
        if (data != nullptr) {
            for (std::size_t i = 0; i < clearData.size(); i += bytesPerPixel) {
                std::memcpy(clearData.data() + i, data, bytesPerPixel);
            }
        } else {
            std::memset(clearData.data(), 0, clearData.size());
        }
        texture->replaceRegion(
            MTL::Region::Make2D(0, 0, width, height),
            static_cast<NS::UInteger>(level),
            clearData.data(),
            static_cast<NS::UInteger>(width * bytesPerPixel)
        );

        // Mirror the clear into the image atomic scratch buffer so atomic
        // image-min sees the same starting state as the texture itself.
        if (pixelFormat == MTL::PixelFormatR32Sint) {
            if (MTL::Buffer *scratch = lookupImageAtomicScratchBuffer(texture);
                scratch != nullptr) {
                std::memcpy(scratch->contents(), clearData.data(), clearData.size());
            }
        }
    }

    void finish() override {
        // Block until prior GPU work completes, then start a fresh command
        // buffer for subsequent encoders. Mirrors OpenGL glFinish(). The
        // fresh-buffer step is required because Metal encoders cannot
        // record into a committed buffer; without it, the next dispatch
        // or draw silently no-ops. Same pattern as readDefaultFramebuffer()
        // above.
        auto *commandBuffer = metalCommandBuffer();
        if (commandBuffer == nullptr) {
            return;
        }
        commandBuffer->commit();
        commandBuffer->waitUntilCompleted();
        releaseDeferredMetalBuffers();
        setMetalCommandBuffer(metalCommandQueue()->commandBuffer());
    }
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

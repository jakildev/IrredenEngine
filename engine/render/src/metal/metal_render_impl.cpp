#include <irreden/render/metal/metal_render_impl.hpp>
#include <irreden/render/buffer.hpp>
#include <irreden/render/metal/metal_cocoa_bridge.hpp>
#include <irreden/render/metal/metal_runtime.hpp>
#include <irreden/render/render_device.hpp>
#include <irreden/render/texture.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

namespace IRRender {

namespace {

constexpr std::uint32_t kMaxMetalBindings = 32;
constexpr NS::UInteger kTimestampAttachmentIndex = 0;

struct MetalTimestampPair {
    MTL::CounterSampleBuffer *sampleBuffer_ = nullptr;
    bool hasStart_ = false;
    bool hasEnd_ = false;
};

struct MetalTimestampSampleAttachment {
    MTL::CounterSampleBuffer *sampleBuffer_ = nullptr;
    NS::UInteger startSampleIndex_ = 0;
    NS::UInteger endSampleIndex_ = 1;
    // Sticky across every encoder of a stage (the window between
    // writeTimestamp START and END): the first encoder claims the start
    // boundary; every encoder re-writes the end boundary. Sequential GPU
    // execution makes the last encoder's end write win, so the resolved pair
    // spans [first-encoder start, last-encoder end] — the whole stage, not
    // just its first encoder (#1746).
    bool firstEncoder_ = true;
};

MetalTimestampSampleAttachment g_nextComputeTimestampAttachment;

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
            markMetalBufferEncoded(uniform.buffer_);
        }

        const auto &storage = boundMetalBuffer(BufferTarget::SHADER_STORAGE, i);
        if (storage.buffer_ != nullptr) {
            encoder->setVertexBuffer(storage.buffer_, storage.offset_, i);
            encoder->setFragmentBuffer(storage.buffer_, storage.offset_, i);
            markMetalBufferEncoded(storage.buffer_);
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
            markMetalBufferEncoded(uniform.buffer_);
        }

        const auto &storage = boundMetalBuffer(BufferTarget::SHADER_STORAGE, i);
        if (storage.buffer_ != nullptr) {
            encoder->setBuffer(storage.buffer_, storage.offset_, i);
            markMetalBufferEncoded(storage.buffer_);
        }

        if (MTL::Texture *texture = boundMetalTexture(i); texture != nullptr) {
            encoder->setTexture(texture, i);
        }
        if (MTL::Texture *imageTexture = boundMetalImageTexture(i); imageTexture != nullptr) {
            encoder->setTexture(imageTexture, i);
        }
    }

    // Image atomic scratch buffer (mirrors the canvas distance R32I texture).
    // See metal_runtime.hpp for the rationale; binding slot is fixed. Bound
    // ONLY for kernels registered as scratch consumers: the scratch pointer is
    // sticky (set by every R32I bindAsImage, never cleared), and slot 16
    // doubles as kBufferIndex_RevoxelizeDetachedParams — an unconditional bind
    // clobbered c_revoxelize_detached's params UBO on every encode after the
    // first distance-image bind of the app's lifetime (#1619).
    if (MTL::Buffer *scratch = currentImageAtomicScratch(); scratch != nullptr) {
        const MetalPipelineStateProvider *pipeline = activeMetalPipeline();
        if (pipeline != nullptr && pipeline->usesImageAtomicScratch()) {
            encoder->setBuffer(scratch, 0, kMetalImageAtomicScratchSlot);
            markMetalBufferEncoded(scratch);
        }
    }
}

MTL::ComputeCommandEncoder *createComputeEncoder(MTL::CommandBuffer *commandBuffer) {
    if (commandBuffer == nullptr) {
        return nullptr;
    }
    if (g_nextComputeTimestampAttachment.sampleBuffer_ == nullptr) {
        return commandBuffer->computeCommandEncoder();
    }

    auto *descriptor = MTL::ComputePassDescriptor::alloc()->init();
    auto *attachment = descriptor->sampleBufferAttachments()->object(kTimestampAttachmentIndex);
    attachment->setSampleBuffer(g_nextComputeTimestampAttachment.sampleBuffer_);
    attachment->setStartOfEncoderSampleIndex(
        g_nextComputeTimestampAttachment.firstEncoder_
            ? g_nextComputeTimestampAttachment.startSampleIndex_
            : MTL::CounterDontSample
    );
    attachment->setEndOfEncoderSampleIndex(g_nextComputeTimestampAttachment.endSampleIndex_);
    auto *encoder = commandBuffer->computeCommandEncoder(descriptor);
    descriptor->release();

    // Stay sticky for the rest of the stage's encoders (each re-writes the end
    // boundary; the last one wins). writeTimestamp(END) clears the attachment.
    g_nextComputeTimestampAttachment.firstEncoder_ = false;
    return encoder;
}

void attachTimestampSamples(MTL::RenderPassDescriptor *descriptor) {
    if (descriptor == nullptr || g_nextComputeTimestampAttachment.sampleBuffer_ == nullptr) {
        return;
    }

    auto *attachment = descriptor->sampleBufferAttachments()->object(kTimestampAttachmentIndex);
    attachment->setSampleBuffer(g_nextComputeTimestampAttachment.sampleBuffer_);
    attachment->setStartOfVertexSampleIndex(
        g_nextComputeTimestampAttachment.firstEncoder_
            ? g_nextComputeTimestampAttachment.startSampleIndex_
            : MTL::CounterDontSample
    );
    attachment->setEndOfFragmentSampleIndex(g_nextComputeTimestampAttachment.endSampleIndex_);
    // Sticky across the stage's encoders (the shared global means a stage that
    // mixes compute + render encoders has its first-executed encoder claim the
    // start boundary). writeTimestamp(END) clears the attachment.
    g_nextComputeTimestampAttachment.firstEncoder_ = false;
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
    attachTimestampSamples(renderPassDescriptor);
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

MTL::CounterSet *findTimestampCounterSet(MTL::Device *device) {
    if (device == nullptr) {
        return nullptr;
    }
    auto *counterSets = device->counterSets();
    if (counterSets == nullptr) {
        return nullptr;
    }
    for (NS::UInteger i = 0; i < counterSets->count(); ++i) {
        auto *counterSet = static_cast<MTL::CounterSet *>(counterSets->object(i));
        if (counterSet != nullptr &&
            counterSet->name() != nullptr &&
            counterSet->name()->isEqualToString(MTL::CommonCounterSetTimestamp)) {
            return counterSet;
        }
    }
    return nullptr;
}

class MetalRenderDevice final : public RenderDevice {
  public:
    void init(MTL::Device *device, CA::MetalLayer *layer) {
        initializeMetalRuntime(device, layer);
        bindMetalDefaultRenderTarget();
        m_timestampCounterSet = findTimestampCounterSet(device);
        m_supportsTimestampPairs =
            m_timestampCounterSet != nullptr &&
            device->supportsCounterSampling(MTL::CounterSamplingPointAtStageBoundary);
        if (!m_supportsTimestampPairs) {
            IR_LOG_WARN(
                "Metal timestamp counter sampling is unavailable; GPU stage timing will use legacy finish() fallback."
            );
        }
    }

    void shutdown() {
        for (auto &pair : m_timestamps) {
            releaseTimestampPair(pair);
        }
        m_timestamps.clear();
        // counterSets() returns a non-owning pointer (the MTL::Device owns
        // the set), so we clear the reference without ->release().
        m_timestampCounterSet = nullptr;
        m_supportsTimestampPairs = false;
        for (auto &[texture, buf] : m_clearSourceBuffers) {
            if (buf != nullptr) {
                buf->release();
            }
        }
        m_clearSourceBuffers.clear();
        shutdownMetalRuntime();
    }

    void releaseClearSourceBuffer(MTL::Texture *texture) {
        if (texture == nullptr) {
            return;
        }
        const auto it = m_clearSourceBuffers.find(texture);
        if (it == m_clearSourceBuffers.end()) {
            return;
        }
        if (it->second != nullptr) {
            it->second->release();
        }
        m_clearSourceBuffers.erase(it);
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

        auto *encoder = createComputeEncoder(commandBuffer);
        if (encoder == nullptr) {
            return;
        }
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

        auto *encoder = createComputeEncoder(commandBuffer);
        if (encoder == nullptr) {
            return;
        }
        encoder->setComputePipelineState(pipeline->getComputePipelineState());
        bindComputeResources(encoder);
        markMetalBufferEncoded(mtlIndirectBuffer);
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
        markMetalBufferEncoded(layout.vertexBuffer_);
        markMetalBufferEncoded(layout.indexBuffer_);
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
        markMetalBufferEncoded(layout.vertexBuffer_);
        markMetalBufferEncoded(layout.indexBuffer_);
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

    void drawElementsInstancedIndirect(
        DrawMode drawMode,
        IndexType indexType,
        const Buffer *indirectBuffer,
        std::ptrdiff_t indirectOffset
    ) override {
        if (indirectBuffer == nullptr) {
            return;
        }
        auto *pipeline = activeMetalPipeline();
        const auto &layout = activeMetalVertexLayout();
        if (pipeline == nullptr || pipeline->isComputePipeline() || layout.indexBuffer_ == nullptr) {
            return;
        }
        auto *mtlIndirect = static_cast<MTL::Buffer *>(indirectBuffer->getNativeBuffer());
        if (mtlIndirect == nullptr) {
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
        markMetalBufferEncoded(layout.vertexBuffer_);
        markMetalBufferEncoded(layout.indexBuffer_);
        markMetalBufferEncoded(mtlIndirect);
        // Index/instance counts come from the GPU-written indirect args; the
        // leading five uints match MTLDrawIndexedPrimitivesIndirectArguments.
        encoder->drawIndexedPrimitives(
            toMetalPrimitiveType(drawMode),
            toMetalIndexType(indexType),
            layout.indexBuffer_,
            0,
            mtlIndirect,
            static_cast<NS::UInteger>(indirectOffset)
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
        markMetalBufferEncoded(layout.vertexBuffer_);
        encoder->drawPrimitives(
            toMetalPrimitiveType(drawMode),
            static_cast<NS::UInteger>(first),
            static_cast<NS::UInteger>(count)
        );
        encoder->endEncoding();
    }

    void drawArraysInstanced(
        DrawMode drawMode, int first, int count, int instanceCount
    ) override {
        if (instanceCount <= 0) {
            return;
        }
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
        markMetalBufferEncoded(layout.vertexBuffer_);
        encoder->drawPrimitives(
            toMetalPrimitiveType(drawMode),
            static_cast<NS::UInteger>(first),
            static_cast<NS::UInteger>(count),
            static_cast<NS::UInteger>(instanceCount)
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
        std::size_t bytesPerPixel = 4;
        const auto pixelFormat = texture->pixelFormat();
        if (pixelFormat == MTL::PixelFormatRG32Uint ||
            pixelFormat == MTL::PixelFormatRGBA16Float) {
            bytesPerPixel = 8;
        } else if (pixelFormat == MTL::PixelFormatRGBA32Float) {
            bytesPerPixel = 16;
        }
        const std::size_t totalBytes =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * bytesPerPixel;

        // Get or create a persistent SharedMode source buffer for this texture.
        // The buffer is filled once at first call for this texture; data changes
        // on later calls are silently ignored. Current callers always pass the
        // same constant clear pattern, so this is benign.
        // Blitted GPU-side every frame — no per-frame CPU allocation or stall.
        MTL::Buffer *clearBuf = nullptr;
        {
            const auto it = m_clearSourceBuffers.find(texture);
            if (it == m_clearSourceBuffers.end()) {
                clearBuf = metalDevice()->newBuffer(
                    totalBytes, MTL::ResourceStorageModeShared
                );
                IR_ASSERT(clearBuf != nullptr, "Failed to create Metal clearTexImage buffer");
                auto *bytes = static_cast<std::uint8_t *>(clearBuf->contents());
                if (data != nullptr) {
                    for (std::size_t i = 0; i < totalBytes; i += bytesPerPixel) {
                        std::memcpy(bytes + i, data, bytesPerPixel);
                    }
                } else {
                    std::memset(bytes, 0, totalBytes);
                }
                m_clearSourceBuffers[texture] = clearBuf;
            } else {
                clearBuf = it->second;
            }
        }

        auto *commandBuffer = metalCommandBuffer();
        if (commandBuffer != nullptr) {
            // GPU-side blit: no per-frame allocation, no replaceRegion stall.
            auto *blit = commandBuffer->blitCommandEncoder();
            blit->copyFromBuffer(
                clearBuf,
                0,
                static_cast<NS::UInteger>(width * bytesPerPixel),
                totalBytes,
                MTL::Size::Make(width, height, 1),
                texture,
                0,
                static_cast<NS::UInteger>(level),
                MTL::Origin::Make(0, 0, 0)
            );
            blit->endEncoding();

            // Mirror the clear into the image atomic scratch buffer so atomic
            // image-min sees the same starting state as the texture itself.
            if (pixelFormat == MTL::PixelFormatR32Sint) {
                if (MTL::Buffer *scratch = lookupImageAtomicScratchBuffer(texture);
                    scratch != nullptr) {
                    auto *blit2 = commandBuffer->blitCommandEncoder();
                    blit2->copyFromBuffer(clearBuf, 0, scratch, 0, totalBytes);
                    blit2->endEncoding();
                }
            }
        } else {
            // No command buffer (e.g. during startup init): fall back to replaceRegion.
            texture->replaceRegion(
                MTL::Region::Make2D(0, 0, width, height),
                static_cast<NS::UInteger>(level),
                clearBuf->contents(),
                static_cast<NS::UInteger>(width * bytesPerPixel)
            );
            if (pixelFormat == MTL::PixelFormatR32Sint) {
                if (MTL::Buffer *scratch = lookupImageAtomicScratchBuffer(texture);
                    scratch != nullptr) {
                    std::memcpy(scratch->contents(), clearBuf->contents(), totalBytes);
                }
            }
        }
    }

    void fillBuffer(const Buffer *buffer, std::size_t sizeBytes, std::uint8_t byteValue) override {
        if (buffer == nullptr || sizeBytes == 0) {
            return;
        }
        auto *mtlBuffer = static_cast<MTL::Buffer *>(buffer->getNativeBuffer());
        if (mtlBuffer == nullptr) {
            return;
        }
        const std::size_t clamped =
            sizeBytes < mtlBuffer->length() ? sizeBytes : mtlBuffer->length();
        auto *commandBuffer = metalCommandBuffer();
        if (commandBuffer != nullptr) {
            auto *blit = commandBuffer->blitCommandEncoder();
            blit->fillBuffer(
                mtlBuffer, NS::Range::Make(0, static_cast<NS::UInteger>(clamped)), byteValue
            );
            blit->endEncoding();
            markMetalBufferEncoded(mtlBuffer);
        } else {
            // No command buffer (e.g. during startup init) — the GPU can't be
            // reading the buffer yet, so a direct CPU fill is safe.
            std::memset(mtlBuffer->contents(), byteValue, clamped);
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

    GpuTimestampHandle createTimestampPair() override {
        if (!m_supportsTimestampPairs) {
            return kInvalidGpuTimestampHandle;
        }

        auto *descriptor = MTL::CounterSampleBufferDescriptor::alloc()->init();
        descriptor->setCounterSet(m_timestampCounterSet);
        descriptor->setSampleCount(2);
        descriptor->setStorageMode(MTL::StorageModeShared);

        NS::Error *error = nullptr;
        MTL::CounterSampleBuffer *sampleBuffer =
            metalDevice()->newCounterSampleBuffer(descriptor, &error);
        descriptor->release();

        if (sampleBuffer == nullptr) {
            // Per-pair failure (quota exhaustion mid-tagStage loop is the
            // common cause) is non-fatal: stages that already got valid
            // pairs keep timing, the observer no-ops on this stage's invalid
            // handles (see `nextAvailableSlot`), and `useLegacyTiming` does
            // not flip — global state matches the constructor probe, not
            // a per-call result. Log once so the operator notices the
            // degradation but doesn't get spammed.
            if (!m_loggedTimestampAllocFailure) {
                const char *description =
                    error != nullptr && error->localizedDescription() != nullptr
                        ? error->localizedDescription()->utf8String()
                        : "<unknown>";
                IR_LOG_WARN(
                    "Failed to create Metal timestamp counter sample buffer "
                    "(quota likely exhausted; later-tagged stages skip per-stage GPU timing): {}",
                    description
                );
                m_loggedTimestampAllocFailure = true;
            }
            return kInvalidGpuTimestampHandle;
        }

        const GpuTimestampHandle handle = m_nextTimestampHandle++;
        m_timestamps.push_back(MetalTimestampPair{sampleBuffer, false, false});
        return handle;
    }

    void destroyTimestampPair(GpuTimestampHandle handle) override {
        MetalTimestampPair *pair = findTimestampPair(handle);
        if (pair == nullptr) {
            return;
        }
        releaseTimestampPair(*pair);
    }

    bool supportsGpuTimestampPairs() const override {
        return m_supportsTimestampPairs;
    }

    int recommendedTimestampPairsInFlight() const override {
        // Today's Metal pipeline blocks at `present()` (one frame of GPU work
        // in flight, then `waitUntilCompleted`), so a single `MTL::CounterSampleBuffer`
        // per tagged stage is sufficient: by the top of frame N+1 the GPU has
        // already finished frame N's samples and `resolveCounterRange` returns
        // valid data without stalling. Bumping past 1 is wasted on this
        // pipeline and risks exhausting the device's limited sample-buffer
        // quota (Apple Silicon devices observed at ~stages × 2 = ~40-pair
        // ceiling, 2026-05-21). Once `present()` is made async (separate task),
        // raise this to 2-3 so frame N-2 readback survives the deeper
        // GPU latency without dropping per-frame samples. The per-pair
        // graceful-fallback in `createTimestampPair` keeps already-tagged
        // stages timing-functional even when a future bump exceeds quota.
        return 1;
    }

    void writeTimestamp(GpuTimestampHandle handle, TimestampSlot slot) override {
        MetalTimestampPair *pair = findTimestampPair(handle);
        if (pair == nullptr || pair->sampleBuffer_ == nullptr) {
            return;
        }

        if (slot == TimestampSlot::START) {
            g_nextComputeTimestampAttachment = {
                pair->sampleBuffer_,
                0,
                1,
                /*firstEncoder_=*/true
            };
            pair->hasStart_ = true;
            pair->hasEnd_ = false;
        } else {
            // Stop tagging once the stage ends: clear the sticky attachment so
            // encoders created in the gap before the next START stay untracked
            // (#1746). The end boundary was already attached to every encoder
            // of the stage up to this point, so the last one's index-1 write
            // bounds the pair.
            g_nextComputeTimestampAttachment = {};
            pair->hasEnd_ = true;
        }
    }

    bool readTimestampPairMs(GpuTimestampHandle handle, float &outMs) override {
        MetalTimestampPair *pair = findTimestampPair(handle);
        if (pair == nullptr || pair->sampleBuffer_ == nullptr) {
            return false;
        }
        if (!pair->hasStart_ || !pair->hasEnd_) {
            return false;
        }

        NS::Data *data = pair->sampleBuffer_->resolveCounterRange(NS::Range::Make(0, 2));
        if (data == nullptr || data->length() < sizeof(MTL::CounterResultTimestamp) * 2) {
            return false;
        }
        auto *samples = static_cast<MTL::CounterResultTimestamp *>(data->mutableBytes());
        if (samples == nullptr ||
            samples[0].timestamp == MTL::CounterErrorValue ||
            samples[1].timestamp == MTL::CounterErrorValue ||
            samples[1].timestamp < samples[0].timestamp) {
            return false;
        }

        outMs = static_cast<float>(samples[1].timestamp - samples[0].timestamp) / 1'000'000.0f;
        return true;
    }

  private:
    MetalTimestampPair *findTimestampPair(GpuTimestampHandle handle) {
        if (handle == kInvalidGpuTimestampHandle || handle > m_timestamps.size()) {
            return nullptr;
        }
        return &m_timestamps[handle - 1];
    }

    void releaseTimestampPair(MetalTimestampPair &pair) {
        if (pair.sampleBuffer_ != nullptr) {
            pair.sampleBuffer_->release();
            pair.sampleBuffer_ = nullptr;
        }
        pair.hasStart_ = false;
        pair.hasEnd_ = false;
    }

    GpuTimestampHandle m_nextTimestampHandle = 1;
    std::vector<MetalTimestampPair> m_timestamps;
    MTL::CounterSet *m_timestampCounterSet = nullptr;
    bool m_supportsTimestampPairs = false;
    bool m_loggedTimestampAllocFailure = false;
    std::unordered_map<MTL::Texture *, MTL::Buffer *> m_clearSourceBuffers;
};

// Intentionally leaked so the device state outlives all other statics.
// Mirrors the same pattern in metal_runtime.cpp's `g_runtime()`. Prevents
// use-after-destroy when `World`'s `RenderingResourceManager` destructs
// its `Texture2D` resources at static-destruction time: each
// `~MetalTexture2DImpl` calls `removeClearSourceBuffer(m_texture)`, which
// dereferences `m_clearSourceBuffers` on this device. Static-destruction
// order between this TU and the `inline` `g_world` in `ir_engine.hpp` is
// implementation-defined; without the leak, the device can be destroyed
// before the textures, and `find()` crashes on the corpse of its
// unordered_map (T-336).
MetalRenderDevice &metalRenderDevice() {
    static auto *device = new MetalRenderDevice{};
    return *device;
}
} // namespace

void removeClearSourceBuffer(MTL::Texture *texture) {
    metalRenderDevice().releaseClearSourceBuffer(texture);
}

std::unique_ptr<RenderImpl> createRenderer() {
    return std::make_unique<MetalRenderImpl>();
}

MetalRenderImpl::MetalRenderImpl()
    : m_device{MTL::CreateSystemDefaultDevice()} {
    IRE_LOG_INFO("Initializing Metal render implementation.");
    setMetalBootstrapDevice(m_device);
}

MetalRenderImpl::~MetalRenderImpl() {
    metalRenderDevice().shutdown();
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

    metalRenderDevice().init(m_device, m_layer);
    setDevice(&metalRenderDevice());

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

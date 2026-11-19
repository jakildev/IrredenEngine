#include <irreden/render/metal/metal_runtime.hpp>
#include <irreden/render/render_device.hpp>
#include <irreden/ir_profile.hpp>

#include <bit>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace IRRender {

namespace {

constexpr std::size_t kMaxMetalBufferBindings = 32;
constexpr std::size_t kMaxMetalTextureBindings = 32;
constexpr std::size_t kMetalStagingChunkSize = 4u * 1024u * 1024u;
constexpr std::size_t kMetalStagingAlignment = 256;
constexpr std::size_t kMetalStagingIdleDrainLimit = 120;

// A chunk larger than kMetalStagingChunkSize serves only uploads larger than
// that, so small uploads cannot fragment it and leave the large one unserved.
struct MetalStagingChunk {
    MTL::Buffer *buffer_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t cursor_ = 0;
    std::size_t idleDrainCount_ = 0;
};

struct MetalRuntimeState {
    MTL::Device *device_ = nullptr;
    CA::MetalLayer *layer_ = nullptr;
    MTL::CommandQueue *commandQueue_ = nullptr;
    MTL::CommandBuffer *commandBuffer_ = nullptr;
    CA::MetalDrawable *drawable_ = nullptr;
    MTL::DepthStencilState *depthDisabledState_ = nullptr;
    MTL::DepthStencilState *depthTestNoWriteState_ = nullptr;
    MTL::DepthStencilState *depthTestWriteState_ = nullptr;
    bool depthTestEnabled_ = true;
    bool depthWriteEnabled_ = true;

    MetalPipelineStateProvider *activePipeline_ = nullptr;
    MetalVertexLayoutBinding activeVertexLayout_{};

    std::array<MetalBufferBinding, kMaxMetalBufferBindings> uniformBuffers_{};
    std::array<MetalBufferBinding, kMaxMetalBufferBindings> shaderStorageBuffers_{};
    std::array<MTL::Texture *, kMaxMetalTextureBindings> textures_{};
    std::array<MTL::Texture *, kMaxMetalTextureBindings> imageTextures_{};

    bool useDefaultRenderTarget_ = true;
    bool clearRenderTarget_ = false;
    MTL::Texture *offscreenColorTexture_ = nullptr;
    MTL::Texture *colorTexture_ = nullptr;
    MTL::Texture *depthTexture_ = nullptr;
    MTL::Texture *entityIdTexture_ = nullptr;
    MTL::PixelFormat colorPixelFormat_ = MTL::PixelFormatBGRA8Unorm;
    MTL::PixelFormat depthPixelFormat_ = MTL::PixelFormatInvalid;

    // The one MTL::Texture * here that untrackMetalTexture does NOT scrub: the
    // map key is cleared at its own site, releaseImageAtomicScratchBuffer,
    // because the entry owns the mapped MTL::Buffer and has to release it
    // rather than just drop the reference. ~MetalTexture2DImpl calls it; only
    // 2D textures ever reach the map (ensureImageAtomicScratchBuffer's sole
    // caller is the 2D bindAsImage path), so the 3D destructor is complete
    // without it.
    std::unordered_map<MTL::Texture *, MTL::Buffer *> imageAtomicScratchBuffers_;
    MTL::Buffer *currentImageAtomicScratch_ = nullptr;

    struct SubmissionResources {
        std::vector<MTL::Buffer *> pendingReleaseBuffers_;
        std::vector<MetalStagingChunk> stagingChunks_;
        std::unordered_set<MTL::Buffer *> encodedBuffers_;
    };
    SubmissionResources recording_;
    SubmissionResources submitted_;
    std::size_t stagingBufferAllocationCount_ = 0;
    BufferWriteStats bufferWriteStats_;
    DeviceErrorStats deviceErrorStats_;
    std::string firstCommandBufferErrorText_;
    MetalAllocationKind failNextAllocation_ = MetalAllocationKind::BUFFER;
    bool failNextAllocationArmed_ = false;
    bool failNextRenderPipeline_ = false;
    std::unordered_set<std::int64_t> loggedCommandBufferErrorCodes_;
};

// Intentionally leaked so the runtime state outlives all other statics.
// Prevents use-after-destroy when World's RenderManager buffers unregister
// their Metal handles during program shutdown.
MetalRuntimeState &g_runtime() {
    static auto *state = new MetalRuntimeState{};
    return *state;
}

std::array<MetalBufferBinding, kMaxMetalBufferBindings> &bufferBindingsForTarget(BufferTarget target) {
    switch (target) {
        case BufferTarget::UNIFORM:
            return g_runtime().uniformBuffers_;
        case BufferTarget::SHADER_STORAGE:
            return g_runtime().shaderStorageBuffers_;
        default:
            return g_runtime().uniformBuffers_;
    }
}

const std::array<MetalBufferBinding, kMaxMetalBufferBindings> &bufferBindingsForTargetConst(
    BufferTarget target
) {
    switch (target) {
        case BufferTarget::UNIFORM:
            return g_runtime().uniformBuffers_;
        case BufferTarget::SHADER_STORAGE:
            return g_runtime().shaderStorageBuffers_;
        default:
            return g_runtime().uniformBuffers_;
    }
}

} // namespace

MTL::DepthStencilState *createDepthStencilState(
    MTL::Device *device,
    MTL::CompareFunction compareFunction,
    bool writeEnabled
) {
    auto *depthDescriptor = MTL::DepthStencilDescriptor::alloc()->init();
    depthDescriptor->setDepthCompareFunction(compareFunction);
    depthDescriptor->setDepthWriteEnabled(writeEnabled);
    auto *state = device->newDepthStencilState(depthDescriptor);
    depthDescriptor->release();
    if (state == nullptr) {
        throw std::runtime_error("Metal depth-stencil state creation failed");
    }
    return state;
}

void initializeMetalRuntime(MTL::Device *device, CA::MetalLayer *layer) {
    g_runtime().device_ = device;
    g_runtime().layer_ = layer;
    g_runtime().commandQueue_ = device->newCommandQueue();
    if (g_runtime().commandQueue_ == nullptr) {
        throw std::runtime_error("Metal command queue creation failed");
    }
    g_runtime().depthDisabledState_ =
        createDepthStencilState(device, MTL::CompareFunctionAlways, false);
    g_runtime().depthTestNoWriteState_ =
        createDepthStencilState(device, MTL::CompareFunctionLess, false);
    g_runtime().depthTestWriteState_ =
        createDepthStencilState(device, MTL::CompareFunctionLess, true);
}

void setMetalBootstrapDevice(MTL::Device *device) {
    g_runtime().device_ = device;
}

void shutdownMetalRuntime() {
    if (g_runtime().commandQueue_ != nullptr) {
        g_runtime().commandQueue_->release();
        g_runtime().commandQueue_ = nullptr;
    }
    if (g_runtime().depthDisabledState_ != nullptr) {
        g_runtime().depthDisabledState_->release();
        g_runtime().depthDisabledState_ = nullptr;
    }
    if (g_runtime().depthTestNoWriteState_ != nullptr) {
        g_runtime().depthTestNoWriteState_->release();
        g_runtime().depthTestNoWriteState_ = nullptr;
    }
    if (g_runtime().depthTestWriteState_ != nullptr) {
        g_runtime().depthTestWriteState_->release();
        g_runtime().depthTestWriteState_ = nullptr;
    }
    releaseDeferredMetalBuffers();
    for (auto &chunk : g_runtime().recording_.stagingChunks_) {
        chunk.buffer_->release();
    }
    for (auto &chunk : g_runtime().submitted_.stagingChunks_) {
        chunk.buffer_->release();
    }
    g_runtime() = MetalRuntimeState{};
}

void resizeMetalDrawable(int width, int height) {
    if (g_runtime().layer_ != nullptr) {
        g_runtime().layer_->setDrawableSize(CGSizeMake(width, height));
    }
}

MTL::Device *metalDevice() {
    return g_runtime().device_;
}

CA::MetalLayer *metalLayer() {
    return g_runtime().layer_;
}

MTL::CommandQueue *metalCommandQueue() {
    return g_runtime().commandQueue_;
}

MTL::CommandBuffer *metalCommandBuffer() {
    return g_runtime().commandBuffer_;
}

CA::MetalDrawable *metalDrawable() {
    return g_runtime().drawable_;
}

MTL::DepthStencilState *currentMetalDepthStencilState() {
    if (!g_runtime().depthTestEnabled_) {
        return g_runtime().depthDisabledState_;
    }
    if (g_runtime().depthWriteEnabled_) {
        return g_runtime().depthTestWriteState_;
    }
    return g_runtime().depthTestNoWriteState_;
}

MTL::Buffer *newMetalBuffer(
    std::size_t byteCount,
    MTL::ResourceOptions options,
    std::string_view resourceKind
) {
    auto &runtime = g_runtime();
    MTL::Buffer *buffer = nullptr;
    if (!(runtime.failNextAllocationArmed_ &&
          runtime.failNextAllocation_ == MetalAllocationKind::BUFFER)) {
        buffer = runtime.device_->newBuffer(static_cast<NS::UInteger>(byteCount), options);
    } else {
        runtime.failNextAllocationArmed_ = false;
    }
    if (buffer == nullptr) {
        IRE_LOG_ERROR(
            "Metal {} allocation failed: requested {} bytes", resourceKind, byteCount
        );
        throw std::runtime_error(
            "Metal " + std::string(resourceKind) + " allocation failed: requested " +
            std::to_string(byteCount) + " bytes"
        );
    }
    return buffer;
}

MTL::Texture *newMetalTexture(
    MTL::TextureDescriptor *descriptor,
    MetalAllocationKind kind,
    std::string_view resourceKind
) {
    auto &runtime = g_runtime();
    MTL::Texture *texture = nullptr;
    if (!(runtime.failNextAllocationArmed_ && runtime.failNextAllocation_ == kind)) {
        texture = runtime.device_->newTexture(descriptor);
    } else {
        runtime.failNextAllocationArmed_ = false;
    }
    if (texture == nullptr) {
        IRE_LOG_ERROR(
            "Metal {} allocation failed: requested {}x{}x{}",
            resourceKind,
            descriptor->width(),
            descriptor->height(),
            descriptor->depth()
        );
        throw std::runtime_error(
            "Metal " + std::string(resourceKind) + " allocation failed: requested " +
            std::to_string(descriptor->width()) + "x" +
            std::to_string(descriptor->height()) + "x" +
            std::to_string(descriptor->depth())
        );
    }
    return texture;
}

void failNextMetalAllocationForTesting(MetalAllocationKind kind) {
    g_runtime().failNextAllocation_ = kind;
    g_runtime().failNextAllocationArmed_ = true;
}

void recordMetalCommandBufferCompletion(
    MTL::CommandBufferStatus status,
    std::int64_t errorCode,
    std::string_view errorText,
    std::uint64_t frameNumber
) {
    if (status != MTL::CommandBufferStatusError) {
        return;
    }
    auto &runtime = g_runtime();
    DeviceErrorStats &stats = runtime.deviceErrorStats_;
    ++stats.commandBufferErrors_;
    if (stats.firstCommandBufferError_.empty()) {
        runtime.firstCommandBufferErrorText_ = errorText;
        stats.firstCommandBufferError_ = runtime.firstCommandBufferErrorText_;
        stats.firstCommandBufferErrorFrame_ = frameNumber;
    }
    if (runtime.loggedCommandBufferErrorCodes_.insert(errorCode).second) {
        IRE_LOG_ERROR(
            "Metal command buffer failed (code {}, frame {}): {}",
            errorCode,
            frameNumber,
            errorText
        );
    }
}

void recordMetalSkippedFrame() {
    ++g_runtime().deviceErrorStats_.skippedFrames_;
}

DeviceErrorStats &metalDeviceErrorStats() {
    return g_runtime().deviceErrorStats_;
}

void failNextMetalRenderPipelineForTesting() {
    g_runtime().failNextRenderPipeline_ = true;
}

bool consumeMetalRenderPipelineFailureForTesting() {
    const bool fail = g_runtime().failNextRenderPipeline_;
    g_runtime().failNextRenderPipeline_ = false;
    return fail;
}

void setMetalCommandBuffer(MTL::CommandBuffer *commandBuffer) {
    g_runtime().commandBuffer_ = commandBuffer;
}

void setMetalDrawable(CA::MetalDrawable *drawable) {
    g_runtime().drawable_ = drawable;
}

void setMetalDepthTestEnabled(bool enabled) {
    g_runtime().depthTestEnabled_ = enabled;
}

void setMetalDepthWriteEnabled(bool enabled) {
    g_runtime().depthWriteEnabled_ = enabled;
}

void setActiveMetalPipeline(MetalPipelineStateProvider *pipeline) {
    g_runtime().activePipeline_ = pipeline;
}

MetalPipelineStateProvider *activeMetalPipeline() {
    return g_runtime().activePipeline_;
}

void setActiveMetalVertexLayout(
    MTL::Buffer *vertexBuffer,
    MTL::Buffer *indexBuffer,
    MTL::VertexDescriptor *vertexDescriptor,
    const MetalVertexLayoutSignature &signature
) {
    g_runtime().activeVertexLayout_ = {vertexBuffer, indexBuffer, vertexDescriptor, signature};
}

const MetalVertexLayoutBinding &activeMetalVertexLayout() {
    return g_runtime().activeVertexLayout_;
}

MetalRenderPipelineKey currentMetalRenderPipelineKey(bool blendingEnabled) {
    const auto &runtime = g_runtime();
    return MetalRenderPipelineKey{
        .colorPixelFormat_ = runtime.colorPixelFormat_,
        .depthPixelFormat_ = runtime.depthPixelFormat_,
        .entityIdPixelFormat_ = runtime.entityIdTexture_ != nullptr
                                    ? runtime.entityIdTexture_->pixelFormat()
                                    : MTL::PixelFormatInvalid,
        .blendingEnabled_ = blendingEnabled,
        .vertexLayout_ = runtime.activeVertexLayout_.signature_,
    };
}

void bindMetalBuffer(BufferTarget target, std::uint32_t index, MTL::Buffer *buffer, NS::UInteger offset) {
    if (index >= kMaxMetalBufferBindings) {
        return;
    }
    bufferBindingsForTarget(target)[index] = {buffer, offset};
}

const MetalBufferBinding &boundMetalBuffer(BufferTarget target, std::uint32_t index) {
    static const MetalBufferBinding kEmptyBinding{};
    if (index >= kMaxMetalBufferBindings) {
        return kEmptyBinding;
    }
    return bufferBindingsForTargetConst(target)[index];
}

void bindMetalTexture(std::uint32_t unit, MTL::Texture *texture) {
    if (unit >= kMaxMetalTextureBindings) {
        return;
    }
    g_runtime().textures_[unit] = texture;
    // Metal flattens the sampler and image namespaces into ONE setTexture slot
    // space, so the two sticky tables are really one slot per unit: the most
    // recent bind wins. Evicting the sibling entry here is what enforces that —
    // otherwise both tables hold an entry and bindComputeResources' flush ORDER
    // silently decides the winner. A stale image from an earlier dispatch can
    // then shadow every sampler read at the
    // same unit — no error, no warning). An unbind (nullptr) clears only its own
    // table, so it can't evict a live sibling bind.
    if (texture != nullptr) {
        g_runtime().imageTextures_[unit] = nullptr;
    }
}

MTL::Texture *boundMetalTexture(std::uint32_t unit) {
    if (unit >= kMaxMetalTextureBindings) {
        return nullptr;
    }
    return g_runtime().textures_[unit];
}

void bindMetalImageTexture(std::uint32_t unit, MTL::Texture *texture) {
    if (unit >= kMaxMetalTextureBindings) {
        return;
    }
    g_runtime().imageTextures_[unit] = texture;
    // Mirror of bindMetalTexture's eviction — see the rationale there.
    if (texture != nullptr) {
        g_runtime().textures_[unit] = nullptr;
    }
}

MTL::Texture *boundMetalImageTexture(std::uint32_t unit) {
    if (unit >= kMaxMetalTextureBindings) {
        return nullptr;
    }
    return g_runtime().imageTextures_[unit];
}

void untrackMetalTexture(MTL::Texture *texture) {
    if (texture == nullptr) {
        return;
    }
    auto &runtime = g_runtime();
    for (std::uint32_t unit = 0; unit < kMaxMetalTextureBindings; ++unit) {
        if (runtime.textures_[unit] == texture) {
            runtime.textures_[unit] = nullptr;
        }
        if (runtime.imageTextures_[unit] == texture) {
            runtime.imageTextures_[unit] = nullptr;
        }
    }
    // The render-target pair is sticky the same way the bind slots are: only a
    // later bind clears it, and no bind is reachable from a destructor. Fall
    // back to the default target rather than nulling in place — a bare null
    // leaves useDefaultRenderTarget_ false with no color attachment, and
    // createRenderEncoder then returns nullptr for every later pass, silently
    // rendering nothing. Same scrub MetalFramebufferImpl::unbind() performs.
    if (runtime.colorTexture_ == texture || runtime.depthTexture_ == texture ||
        runtime.entityIdTexture_ == texture) {
        bindMetalDefaultRenderTarget();
    }
}

void setMetalOffscreenColorTexture(MTL::Texture *texture) {
    g_runtime().offscreenColorTexture_ = texture;
}

MTL::Texture *metalDefaultColorTexture() {
    if (g_runtime().offscreenColorTexture_ != nullptr) {
        return g_runtime().offscreenColorTexture_;
    }
    auto *drawable = g_runtime().drawable_;
    return drawable != nullptr ? drawable->texture() : nullptr;
}

void bindMetalDefaultRenderTarget() {
    g_runtime().useDefaultRenderTarget_ = true;
    g_runtime().colorTexture_ = nullptr;
    g_runtime().depthTexture_ = nullptr;
    g_runtime().entityIdTexture_ = nullptr;
    g_runtime().colorPixelFormat_ = MTL::PixelFormatBGRA8Unorm;
    g_runtime().depthPixelFormat_ = MTL::PixelFormatInvalid;
}

void bindMetalFramebufferRenderTarget(
    MTL::Texture *colorTexture,
    MTL::Texture *depthTexture,
    MTL::PixelFormat colorPixelFormat,
    MTL::PixelFormat depthPixelFormat,
    MTL::Texture *entityIdTexture
) {
    g_runtime().useDefaultRenderTarget_ = false;
    g_runtime().colorTexture_ = colorTexture;
    g_runtime().depthTexture_ = depthTexture;
    g_runtime().entityIdTexture_ = entityIdTexture;
    g_runtime().colorPixelFormat_ = colorPixelFormat;
    g_runtime().depthPixelFormat_ = depthPixelFormat;
}

void requestMetalRenderTargetClear() {
    g_runtime().clearRenderTarget_ = true;
}

bool consumeMetalRenderTargetClear() {
    const bool clear = g_runtime().clearRenderTarget_;
    g_runtime().clearRenderTarget_ = false;
    return clear;
}

bool metalUsesDefaultRenderTarget() {
    return g_runtime().useDefaultRenderTarget_;
}

MTL::Texture *metalCurrentColorTexture() {
    return g_runtime().colorTexture_;
}

MTL::Texture *metalCurrentEntityIdTexture() {
    return g_runtime().entityIdTexture_;
}

MTL::Texture *metalCurrentDepthTexture() {
    return g_runtime().depthTexture_;
}

MTL::PixelFormat metalCurrentColorPixelFormat() {
    return g_runtime().colorPixelFormat_;
}

MTL::PixelFormat metalCurrentDepthPixelFormat() {
    return g_runtime().depthPixelFormat_;
}

void deferReleaseMetalBuffer(MTL::Buffer *buffer) {
    if (buffer == nullptr) {
        return;
    }
    g_runtime().recording_.pendingReleaseBuffers_.push_back(buffer);
}

namespace {
void recycleSubmission(MetalRuntimeState::SubmissionResources &resources) {
    for (auto *buffer : resources.pendingReleaseBuffers_) {
        buffer->release();
    }
    resources.pendingReleaseBuffers_.clear();
    resources.encodedBuffers_.clear();
    auto &chunks = resources.stagingChunks_;
    for (auto chunk = chunks.begin(); chunk != chunks.end();) {
        chunk->idleDrainCount_ = chunk->cursor_ == 0 ? chunk->idleDrainCount_ + 1 : 0;
        chunk->cursor_ = 0;
        if (chunk->idleDrainCount_ >= kMetalStagingIdleDrainLimit) {
            chunk->buffer_->release();
            chunk = chunks.erase(chunk);
        } else {
            ++chunk;
        }
    }
}
} // namespace

void completeMetalSubmission() {
    recycleSubmission(g_runtime().submitted_);
}

void submitMetalResources() {
    std::swap(g_runtime().recording_, g_runtime().submitted_);
}

void releaseDeferredMetalBuffers() {
    recycleSubmission(g_runtime().submitted_);
    recycleSubmission(g_runtime().recording_);
}

std::size_t deferredMetalBufferReleaseCount() {
    return g_runtime().recording_.pendingReleaseBuffers_.size() +
           g_runtime().submitted_.pendingReleaseBuffers_.size();
}

MetalStagingBufferSlice reserveMetalStaging(std::size_t byteCount) {
    IR_ASSERT(byteCount > 0, "Metal staging byte count must be positive");

    auto &runtime = g_runtime();
    const bool oversized = byteCount > kMetalStagingChunkSize;
    for (auto &chunk : runtime.recording_.stagingChunks_) {
        if ((chunk.capacity_ > kMetalStagingChunkSize) != oversized) {
            continue;
        }
        const std::size_t alignedOffset =
            (chunk.cursor_ + kMetalStagingAlignment - 1) & ~(kMetalStagingAlignment - 1);
        if (alignedOffset > chunk.capacity_ || byteCount > chunk.capacity_ - alignedOffset) {
            continue;
        }
        chunk.cursor_ = alignedOffset + byteCount;
        return {chunk.buffer_, static_cast<NS::UInteger>(alignedOffset)};
    }

    // Power-of-two capacities bound how often a growing upload reallocates.
    const std::size_t capacity = oversized ? std::bit_ceil(byteCount) : kMetalStagingChunkSize;
    auto *buffer = newMetalBuffer(
        capacity, MTL::ResourceStorageModeShared, "staging arena buffer"
    );
    ++runtime.stagingBufferAllocationCount_;
    runtime.recording_.stagingChunks_.push_back({buffer, capacity, byteCount, 0});
    return {buffer, 0, capacity};
}

MetalStagingBufferSlice stageMetalUpload(const void *data, std::size_t byteCount) {
    IR_ASSERT(data != nullptr, "Metal staging data must not be null");
    const MetalStagingBufferSlice slice = reserveMetalStaging(byteCount);
    std::memcpy(slice.contents(), data, byteCount);
    return slice;
}

std::size_t metalStagingBufferAllocationCount() {
    return g_runtime().stagingBufferAllocationCount_;
}

BufferWriteStats &metalBufferWriteStats() {
    return g_runtime().bufferWriteStats_;
}

void markMetalBufferEncoded(MTL::Buffer *buffer) {
    if (buffer == nullptr) {
        return;
    }
    g_runtime().recording_.encodedBuffers_.insert(buffer);
}

bool wasMetalBufferEncoded(MTL::Buffer *buffer) {
    if (buffer == nullptr) {
        return false;
    }
    const auto &set = g_runtime().recording_.encodedBuffers_;
    return set.contains(buffer) || g_runtime().submitted_.encodedBuffers_.contains(buffer);
}

void replaceMetalBufferInBindings(MTL::Buffer *oldBuffer, MTL::Buffer *newBuffer) {
    if (oldBuffer == nullptr || oldBuffer == newBuffer) {
        return;
    }
    for (auto &binding : g_runtime().uniformBuffers_) {
        if (binding.buffer_ == oldBuffer) {
            binding.buffer_ = newBuffer;
        }
    }
    for (auto &binding : g_runtime().shaderStorageBuffers_) {
        if (binding.buffer_ == oldBuffer) {
            binding.buffer_ = newBuffer;
        }
    }
    auto &vertexLayout = g_runtime().activeVertexLayout_;
    if (vertexLayout.vertexBuffer_ == oldBuffer) {
        vertexLayout.vertexBuffer_ = newBuffer;
    }
    if (vertexLayout.indexBuffer_ == oldBuffer) {
        vertexLayout.indexBuffer_ = newBuffer;
    }
}

void untrackMetalBuffer(MTL::Buffer *buffer) {
    if (buffer == nullptr) {
        return;
    }
    // Null-out every sticky table entry via the backing-retarget helper. A
    // cleared binding keeps its stale offset, which is benign — the flush
    // loops gate on buffer_ != nullptr.
    replaceMetalBufferInBindings(buffer, nullptr);
    // Also drop it from the encoded-since-last-wait set: a future buffer
    // allocated at the same address would otherwise false-positive
    // wasMetalBufferEncoded and stage its first subData for no reason.
    g_runtime().recording_.encodedBuffers_.erase(buffer);
    g_runtime().submitted_.encodedBuffers_.erase(buffer);
}

MTL::Buffer *ensureImageAtomicScratchBuffer(MTL::Texture *texture) {
    if (texture == nullptr) {
        return nullptr;
    }
    auto &cache = g_runtime().imageAtomicScratchBuffers_;
    const auto it = cache.find(texture);
    if (it != cache.end()) {
        return it->second;
    }
    const std::size_t bytes =
        static_cast<std::size_t>(texture->width()) *
        static_cast<std::size_t>(texture->height()) *
        sizeof(std::int32_t);
    auto *buffer = newMetalBuffer(
        bytes, MTL::ResourceStorageModeShared, "image-atomic scratch buffer"
    );
    std::memset(buffer->contents(), 0, bytes);
    cache[texture] = buffer;
    return buffer;
}

MTL::Buffer *lookupImageAtomicScratchBuffer(MTL::Texture *texture) {
    if (texture == nullptr) {
        return nullptr;
    }
    auto &cache = g_runtime().imageAtomicScratchBuffers_;
    const auto it = cache.find(texture);
    return it != cache.end() ? it->second : nullptr;
}

void releaseImageAtomicScratchBuffer(MTL::Texture *texture) {
    if (texture == nullptr) {
        return;
    }
    auto &cache = g_runtime().imageAtomicScratchBuffers_;
    const auto it = cache.find(texture);
    if (it == cache.end()) {
        return;
    }
    if (it->second != nullptr) {
        // Same dangling-rebind hazard as untrackMetalBuffer: the sticky
        // current-scratch pointer would re-bind the freed buffer on the next
        // scratch-consumer dispatch if no R32I bindAsImage refreshed it first.
        if (g_runtime().currentImageAtomicScratch_ == it->second) {
            g_runtime().currentImageAtomicScratch_ = nullptr;
        }
        it->second->release();
    }
    cache.erase(it);
}

void setCurrentImageAtomicScratch(MTL::Buffer *buffer) {
    g_runtime().currentImageAtomicScratch_ = buffer;
}

MTL::Buffer *currentImageAtomicScratch() {
    return g_runtime().currentImageAtomicScratch_;
}

} // namespace IRRender

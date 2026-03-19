#include <irreden/render/metal/metal_runtime.hpp>
#include <irreden/ir_profile.hpp>

#include <unordered_map>

namespace IRRender {

namespace {

constexpr std::size_t kMaxMetalBufferBindings = 32;
constexpr std::size_t kMaxMetalTextureBindings = 32;

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
    MTL::Texture *colorTexture_ = nullptr;
    MTL::Texture *depthTexture_ = nullptr;
    MTL::PixelFormat colorPixelFormat_ = MTL::PixelFormatBGRA8Unorm;
    MTL::PixelFormat depthPixelFormat_ = MTL::PixelFormatInvalid;

    std::unordered_map<std::uint32_t, MTL::Buffer *> buffersByHandle_;
    std::uint32_t nextBufferHandle_ = 1;
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
    IR_ASSERT(state != nullptr, "Failed to create Metal depth stencil state");
    return state;
}

void initializeMetalRuntime(MTL::Device *device, CA::MetalLayer *layer) {
    g_runtime().device_ = device;
    g_runtime().layer_ = layer;
    g_runtime().commandQueue_ = device->newCommandQueue();
    IR_ASSERT(g_runtime().commandQueue_ != nullptr, "Failed to create Metal command queue");
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
    MTL::VertexDescriptor *vertexDescriptor
) {
    g_runtime().activeVertexLayout_ = {vertexBuffer, indexBuffer, vertexDescriptor};
}

const MetalVertexLayoutBinding &activeMetalVertexLayout() {
    return g_runtime().activeVertexLayout_;
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
}

MTL::Texture *boundMetalImageTexture(std::uint32_t unit) {
    if (unit >= kMaxMetalTextureBindings) {
        return nullptr;
    }
    return g_runtime().imageTextures_[unit];
}

void bindMetalDefaultRenderTarget() {
    g_runtime().useDefaultRenderTarget_ = true;
    g_runtime().colorTexture_ = nullptr;
    g_runtime().depthTexture_ = nullptr;
    g_runtime().colorPixelFormat_ = MTL::PixelFormatBGRA8Unorm;
    g_runtime().depthPixelFormat_ = MTL::PixelFormatInvalid;
}

void bindMetalFramebufferRenderTarget(
    MTL::Texture *colorTexture,
    MTL::Texture *depthTexture,
    MTL::PixelFormat colorPixelFormat,
    MTL::PixelFormat depthPixelFormat
) {
    g_runtime().useDefaultRenderTarget_ = false;
    g_runtime().colorTexture_ = colorTexture;
    g_runtime().depthTexture_ = depthTexture;
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

MTL::Texture *metalCurrentDepthTexture() {
    return g_runtime().depthTexture_;
}

MTL::PixelFormat metalCurrentColorPixelFormat() {
    return g_runtime().colorPixelFormat_;
}

MTL::PixelFormat metalCurrentDepthPixelFormat() {
    return g_runtime().depthPixelFormat_;
}

std::uint32_t registerMetalBufferHandle(MTL::Buffer *buffer) {
    const std::uint32_t handle = g_runtime().nextBufferHandle_++;
    g_runtime().buffersByHandle_[handle] = buffer;
    return handle;
}

void unregisterMetalBufferHandle(std::uint32_t handle) {
    g_runtime().buffersByHandle_.erase(handle);
}

MTL::Buffer *lookupMetalBufferHandle(std::uint32_t handle) {
    const auto it = g_runtime().buffersByHandle_.find(handle);
    return it != g_runtime().buffersByHandle_.end() ? it->second : nullptr;
}

} // namespace IRRender

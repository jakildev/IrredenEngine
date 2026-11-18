// Consecutive draws to one target share a Metal render pass, and every event
// that must separate them ends it. Each boundary case brackets the event with
// two draws and counts the passes RenderDevice::deviceStats() reports; the
// control with no event between its draws counts one.

#include <gtest/gtest.h>

#if defined(IR_GRAPHICS_METAL)

#include <irreden/ir_math.hpp>
#include <irreden/render/buffer.hpp>
#include <irreden/render/framebuffer.hpp>
#include <irreden/render/ir_render_enums.hpp>
#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/metal/metal_runtime.hpp>
#include <irreden/render/render_device.hpp>
#include <irreden/render/shader.hpp>
#include <irreden/render/shapes_2d.hpp>
#include <irreden/render/texture.hpp>
#include <irreden/render/vao.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace IRRender;

constexpr int kTargetDim = 4;

std::string renderShader(const char *name) {
    return std::string(IR_TEST_RENDER_SHADER_DIR) + "/" + name;
}

// The textured quad spans [-0.5, 0.5]; doubling it covers the whole target.
FrameDataFramebuffer fullTargetFrameData() {
    FrameDataFramebuffer frameData{};
    frameData.mvpMatrix = IRMath::scale(IRMath::mat4(1.0f), IRMath::vec3(2.0f, 2.0f, 1.0f));
    return frameData;
}

// GPU objects outlive no test: they are destroyed before the runtime shuts
// down, because their destructors untrack themselves from it.
struct DrawScene {
    Framebuffer target_{
        IRMath::ivec2(kTargetDim),
        IRMath::ivec2(0),
        TextureFormat::RGBA8,
        TextureFormat::DEPTH24_STENCIL8
    };
    Framebuffer otherTarget_{
        IRMath::ivec2(kTargetDim),
        IRMath::ivec2(0),
        TextureFormat::RGBA8,
        TextureFormat::DEPTH24_STENCIL8
    };
    Texture2D source_{TextureKind::TEXTURE_2D, kTargetDim, kTargetDim, TextureFormat::RGBA8};
    FrameDataFramebuffer frameData_ = fullTargetFrameData();
    Buffer frameDataBuffer_{
        &frameData_,
        sizeof(frameData_),
        BUFFER_STORAGE_DYNAMIC,
        BufferTarget::UNIFORM,
        kBufferIndex_FramebufferFrameDataUniform
    };
    Buffer quadVertices_{IRShapes2D::k2DQuadTextured, sizeof(IRShapes2D::k2DQuadTextured), 0};
    VertexLayout quad_{&quadVertices_, nullptr, 2, kAttrList2Float2};
    ShaderProgram program_{std::vector{
        ShaderStage{renderShader("v_framebuffer_to_screen.glsl").c_str(), ShaderType::VERTEX},
        ShaderStage{renderShader("f_framebuffer_to_screen.glsl").c_str(), ShaderType::FRAGMENT}
    }};
};

class MetalRenderPassTest : public ::testing::Test {
  protected:
    void SetUp() override {
        device_ = bootstrapHeadlessRenderDevice();
        if (device_ == nullptr) {
            GTEST_SKIP() << "No Metal device available — headless host without a GPU.";
        }
        scene_ = std::make_unique<DrawScene>();
        const std::uint8_t texel[4] = {200, 40, 10, 255};
        scene_->source_.clear(PixelDataFormat::RGBA, PixelDataType::UNSIGNED_BYTE, texel);
        scene_->target_.bind();
        useDrawState();
        device_->setDepthTest(false);
        device_->setDepthWrite(false);
        device_->finish();
    }

    void TearDown() override {
        if (device_ == nullptr) {
            return;
        }
        device_->finish();
        device_->setDepthTest(true);
        device_->setDepthWrite(true);
        setActiveMetalPipeline(nullptr);
        scene_.reset();
        MTL::Device *rawDevice = metalDevice();
        shutdownMetalRuntime();
        if (rawDevice != nullptr) {
            rawDevice->release();
        }
        device_ = nullptr;
    }

    void useDrawState() {
        scene_->program_.use();
        scene_->quad_.bind();
        scene_->source_.bind(0);
    }

    void draw() {
        device_->drawArrays(DrawMode::TRIANGLES, 0, 6);
    }

    RenderPassStats passStats() const {
        return device_->deviceStats().renderPasses_;
    }

    // Two draws with @p between run after the first; returns the passes they used.
    template <typename Between> std::uint64_t passesAround(Between between) {
        const RenderPassStats before = passStats();
        draw();
        between();
        draw();
        const RenderPassStats after = passStats();
        EXPECT_EQ(after.draws_ - before.draws_, 2u);
        return after.renderPasses_ - before.renderPasses_;
    }

    RenderDevice *device_ = nullptr;
    std::unique_ptr<DrawScene> scene_;
};

TEST_F(MetalRenderPassTest, ConsecutiveDrawsToOneTargetShareOnePass) {
    EXPECT_EQ(passesAround([] {}), 1u);
    EXPECT_TRUE(metalRenderPassOpen());
}

TEST_F(MetalRenderPassTest, RebindingTheSameTargetKeepsThePass) {
    EXPECT_EQ(passesAround([&] { scene_->target_.bind(); }), 1u);
}

TEST_F(MetalRenderPassTest, ComputeDispatchBetweenDrawsSplitsThePass) {
    Texture2D image{TextureKind::TEXTURE_2D, kTargetDim, kTargetDim, TextureFormat::R32I};
    const std::string writePath = std::string(IR_TEST_GPU_SHADER_DIR) + "/c_r32i_write.glsl";
    ShaderProgram writeProgram{std::vector{ShaderStage{writePath.c_str(), ShaderType::COMPUTE}}};
    EXPECT_EQ(
        passesAround([&] {
            writeProgram.use();
            image.bindAsImage(0, TextureAccess::WRITE_ONLY, TextureFormat::R32I);
            device_->dispatchCompute(kTargetDim, kTargetDim, 1);
            useDrawState();
        }),
        2u
    );
    device_->finish();
}

TEST_F(MetalRenderPassTest, TextureUploadBetweenDrawsSplitsThePass) {
    const std::vector<std::uint8_t> texels(kTargetDim * kTargetDim * 4, 90);
    EXPECT_EQ(
        passesAround([&] {
            scene_->source_.subImage2D(
                0,
                0,
                kTargetDim,
                kTargetDim,
                PixelDataFormat::RGBA,
                PixelDataType::UNSIGNED_BYTE,
                texels.data()
            );
        }),
        2u
    );
}

TEST_F(MetalRenderPassTest, TextureClearBetweenDrawsSplitsThePass) {
    const std::uint8_t texel[4] = {1, 2, 3, 255};
    EXPECT_EQ(
        passesAround([&] {
            scene_->source_.clear(PixelDataFormat::RGBA, PixelDataType::UNSIGNED_BYTE, texel);
        }),
        2u
    );
}

TEST_F(MetalRenderPassTest, BufferCopyBetweenDrawsSplitsThePass) {
    const std::vector<std::uint32_t> words(16, 7u);
    Buffer source{words.data(), words.size() * sizeof(std::uint32_t), BUFFER_STORAGE_NONE};
    Buffer destination{nullptr, words.size() * sizeof(std::uint32_t), BUFFER_STORAGE_NONE};
    EXPECT_EQ(
        passesAround([&] {
            device_->copyBufferSubData(
                &source,
                0,
                &destination,
                0,
                words.size() * sizeof(std::uint32_t)
            );
        }),
        2u
    );
}

TEST_F(MetalRenderPassTest, RenderTargetChangeSplitsThePass) {
    EXPECT_EQ(passesAround([&] { scene_->otherTarget_.bind(); }), 2u);
}

TEST_F(MetalRenderPassTest, RequestedClearSplitsThePass) {
    EXPECT_EQ(passesAround([&] { scene_->target_.clear(); }), 2u);
}

// The second draw is not a valid Metal draw (it samples its own colour
// attachment); the case only proves it never joins the earlier draw's pass.
TEST_F(MetalRenderPassTest, DrawSamplingAnAttachmentSplitsThePass) {
    EXPECT_EQ(passesAround([&] { scene_->target_.getTextureColor().bind(0); }), 2u);
}

TEST_F(MetalRenderPassTest, GpuTimingBoundarySplitsThePass) {
    if (!device_->supportsGpuTimestampPairs()) {
        GTEST_SKIP() << "Metal timestamp counter sampling is unavailable on this device.";
    }
    const GpuTimestampHandle handle = device_->createTimestampPair();
    ASSERT_NE(handle, kInvalidGpuTimestampHandle);
    EXPECT_EQ(passesAround([&] { device_->writeTimestamp(handle, TimestampSlot::START); }), 2u);
    device_->finish();
    EXPECT_EQ(passesAround([&] { device_->writeTimestamp(handle, TimestampSlot::END); }), 2u);
    device_->finish();
    device_->destroyTimestampPair(handle);
}

TEST_F(MetalRenderPassTest, FinishSplitsThePass) {
    EXPECT_EQ(passesAround([&] { device_->finish(); }), 2u);
}

TEST_F(MetalRenderPassTest, MemoryBarrierSplitsThePass) {
    EXPECT_EQ(passesAround([&] { device_->memoryBarrier(BarrierType::SHADER_STORAGE); }), 2u);
}

TEST_F(MetalRenderPassTest, UntrackingAnAttachmentEndsThePass) {
    draw();
    ASSERT_TRUE(metalRenderPassOpen());
    untrackMetalTexture(static_cast<MTL::Texture *>(scene_->source_.getNativeTexture()));
    EXPECT_TRUE(metalRenderPassOpen());
    untrackMetalTexture(
        static_cast<MTL::Texture *>(scene_->target_.getTextureDepth().getNativeTexture())
    );
    EXPECT_FALSE(metalRenderPassOpen());
}

// A shared pass's encoder keeps what the first draw bound, so the second draw
// must clear a slot its predecessor used and it does not: a buffer unbound,
// and a texture destroyed, between the draws. The slots are ones the shader
// never reads, so both draws stay valid under the Metal debug layer.
TEST_F(MetalRenderPassTest, SlotClearedBetweenDrawsIsClearedOnTheSharedPass) {
    constexpr std::uint32_t kBufferSlot = kBufferIndex_ShapeDescriptors;
    constexpr std::uint32_t kTextureUnit = 3;
    const std::vector<std::uint32_t> words(16, 7u);
    Buffer extraBuffer{words.data(), words.size() * sizeof(std::uint32_t), BUFFER_STORAGE_NONE};
    auto extraTexture = std::make_unique<Texture2D>(
        TextureKind::TEXTURE_2D,
        kTargetDim,
        kTargetDim,
        TextureFormat::RGBA8
    );
    auto *nativeBuffer = static_cast<MTL::Buffer *>(extraBuffer.getNativeBuffer());
    auto *nativeTexture = static_cast<MTL::Texture *>(extraTexture->getNativeTexture());
    const auto &bound = metalRenderPassBindings();

    const RenderPassStats before = passStats();
    extraBuffer.bindBase(BufferTarget::SHADER_STORAGE, kBufferSlot);
    extraTexture->bind(kTextureUnit);
    draw();
    EXPECT_EQ(bound.vertexBuffers_[kBufferSlot].buffer_, nativeBuffer);
    EXPECT_EQ(bound.fragmentBuffers_[kBufferSlot].buffer_, nativeBuffer);
    EXPECT_EQ(bound.textures_[kTextureUnit], nativeTexture);
    EXPECT_EQ(
        bound.vertexBuffers_[0].buffer_,
        static_cast<MTL::Buffer *>(scene_->quadVertices_.getNativeBuffer())
    );

    device_->unbindBufferBase(BufferTarget::SHADER_STORAGE, kBufferSlot);
    extraTexture.reset();
    draw();
    EXPECT_EQ(passStats().renderPasses_ - before.renderPasses_, 1u);
    EXPECT_EQ(bound.vertexBuffers_[kBufferSlot], MetalBufferBinding{});
    EXPECT_EQ(bound.fragmentBuffers_[kBufferSlot], MetalBufferBinding{});
    EXPECT_EQ(bound.textures_[kTextureUnit], nullptr);
    EXPECT_EQ(bound.textures_[0], static_cast<MTL::Texture *>(scene_->source_.getNativeTexture()));

    device_->finish();
    EXPECT_EQ(bound.textures_[0], nullptr);
}

// The shared pass must compose its draws exactly as separate passes did: the
// second draw blends over the first one's colour, not over a reloaded target.
TEST_F(MetalRenderPassTest, SharedPassBlendsLaterDrawsOverEarlierOnes) {
    const std::uint8_t destination[4] = {0, 0, 255, 255};
    const std::uint8_t half[4] = {255, 0, 0, 128};
    device_->clearTexImage(&scene_->target_.getTextureColor(), 0, destination);
    scene_->source_.clear(PixelDataFormat::RGBA, PixelDataType::UNSIGNED_BYTE, half);
    device_->enableBlending();
    EXPECT_EQ(passesAround([] {}), 1u);
    device_->disableBlending();
    device_->finish();

    std::vector<std::uint8_t> pixels(kTargetDim * kTargetDim * 4);
    scene_->target_.getTextureColor().getSubImage2D(
        0,
        0,
        kTargetDim,
        kTargetDim,
        PixelDataFormat::RGBA,
        PixelDataType::UNSIGNED_BYTE,
        pixels.data()
    );
    // a = 128/255 and every channel blends v = src * a + v * (1 - a) twice:
    // red 0 -> 128 -> 192, blue 255 -> 127 -> 63, alpha 255 -> 191 -> 159.
    const int expected[4] = {192, 0, 63, 159};
    for (std::size_t byte = 0; byte < pixels.size(); ++byte) {
        ASSERT_LE(IRMath::abs(int(pixels[byte]) - expected[byte % 4]), 2)
            << "texel " << byte / 4 << " channel " << byte % 4 << " = " << int(pixels[byte]);
    }
}

} // namespace

#endif // IR_GRAPHICS_METAL

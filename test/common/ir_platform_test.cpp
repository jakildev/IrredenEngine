#include <gtest/gtest.h>
#include <irreden/ir_platform.hpp>

namespace {

using namespace IRPlatform;

// ─────────────────────────────────────────────
// conventionsFor — pure constexpr, no GPU needed
// ─────────────────────────────────────────────

TEST(ConventionsForTest, OpenGLHasNegativeYDirection) {
    constexpr GraphicsConventions c = conventionsFor(GraphicsBackend::OPENGL);
    EXPECT_FLOAT_EQ(c.screenYDirection_, -1.0f);
}

TEST(ConventionsForTest, OpenGLFlipsMouseY) {
    constexpr GraphicsConventions c = conventionsFor(GraphicsBackend::OPENGL);
    EXPECT_TRUE(c.flipMouseY_);
}

TEST(ConventionsForTest, OpenGLUsesSymmetricNDCDepth) {
    // OpenGL clip-space depth is [-1, 1], so ndcDepthZeroToOne_ is false.
    constexpr GraphicsConventions c = conventionsFor(GraphicsBackend::OPENGL);
    EXPECT_FALSE(c.ndcDepthZeroToOne_);
}

TEST(ConventionsForTest, MetalHasPositiveYDirection) {
    constexpr GraphicsConventions c = conventionsFor(GraphicsBackend::METAL);
    EXPECT_FLOAT_EQ(c.screenYDirection_, 1.0f);
}

TEST(ConventionsForTest, MetalDoesNotFlipMouseY) {
    constexpr GraphicsConventions c = conventionsFor(GraphicsBackend::METAL);
    EXPECT_FALSE(c.flipMouseY_);
}

TEST(ConventionsForTest, MetalUsesZeroToOneNDCDepth) {
    // Metal clip-space depth is [0, 1].
    constexpr GraphicsConventions c = conventionsFor(GraphicsBackend::METAL);
    EXPECT_TRUE(c.ndcDepthZeroToOne_);
}

TEST(ConventionsForTest, VulkanMatchesMetal) {
    // Vulkan shares Metal's clip conventions (positive Y, [0,1] depth).
    constexpr GraphicsConventions vk = conventionsFor(GraphicsBackend::VULKAN);
    constexpr GraphicsConventions mt = conventionsFor(GraphicsBackend::METAL);
    EXPECT_FLOAT_EQ(vk.screenYDirection_, mt.screenYDirection_);
    EXPECT_EQ(vk.flipMouseY_, mt.flipMouseY_);
    EXPECT_EQ(vk.ndcDepthZeroToOne_, mt.ndcDepthZeroToOne_);
}

TEST(ConventionsForTest, OpenGLAndMetalHaveOppositeYDirection) {
    constexpr float ogl = conventionsFor(GraphicsBackend::OPENGL).screenYDirection_;
    constexpr float mtl = conventionsFor(GraphicsBackend::METAL).screenYDirection_;
    EXPECT_FLOAT_EQ(ogl, -mtl);
}

// ─────────────────────────────────────────────
// Platform predicate constants — compile-time consistency
// ─────────────────────────────────────────────

TEST(PlatformPredicatesTest, ExactlyOneOSIsTrue) {
    int count = 0;
    if (kIsLinux)   ++count;
    if (kIsMacOS)   ++count;
    if (kIsWindows) ++count;
    EXPECT_EQ(count, 1) << "exactly one OS predicate must be true";
}

TEST(PlatformPredicatesTest, KGfxMatchesCurrentBackend) {
    // kGfx must equal what conventionsFor(kGraphicsBackend) returns.
    const GraphicsConventions expected = conventionsFor(kGraphicsBackend);
    EXPECT_FLOAT_EQ(kGfx.screenYDirection_, expected.screenYDirection_);
    EXPECT_EQ(kGfx.flipMouseY_, expected.flipMouseY_);
    EXPECT_EQ(kGfx.ndcDepthZeroToOne_, expected.ndcDepthZeroToOne_);
}

TEST(PlatformPredicatesTest, KIsOpenGLConsistentWithBackend) {
    EXPECT_EQ(kIsOpenGL, kGraphicsBackend == GraphicsBackend::OPENGL);
}

} // namespace

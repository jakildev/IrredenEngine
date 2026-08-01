// Executes the sticky-table scrub invariant stated in engine/render/CLAUDE.md:
//
//   "A new resource type that lands in any sticky table must untrack itself
//    the same way."
//
// The runtime's bind slots and its render-target pair both hold non-owning
// MTL::Texture handles that only a later bind ever clears, so a texture
// destroyed while still referenced leaves a dangling pointer that the next
// encode retains (setTexture:) and dereferences (viewport sizing). That
// invariant held by convention alone for two resource types, which is how the
// render-target pair was missed (#2808).
//
// These cases exercise the bookkeeping only: the handles are opaque sentinel
// pointers that are never dereferenced, and no MTL::Device is required, so the
// suite runs headless on any macOS host.

#include <gtest/gtest.h>

#if defined(IR_GRAPHICS_METAL)

#include <irreden/render/metal/metal_runtime.hpp>

#include <cstdint>

namespace {

// Opaque non-null sentinels. untrackMetalTexture and the bind/query helpers
// compare and store the pointer without ever dereferencing it.
MTL::Texture *sentinelTexture(std::uintptr_t id) {
    return reinterpret_cast<MTL::Texture *>(id);
}

} // namespace

TEST(MetalStickyUntrack, ClearsSamplerAndImageBindSlots) {
    auto *texture = sentinelTexture(0x1001);
    IRRender::bindMetalTexture(3, texture);
    IRRender::bindMetalImageTexture(5, texture);
    ASSERT_EQ(IRRender::boundMetalTexture(3), texture);
    ASSERT_EQ(IRRender::boundMetalImageTexture(5), texture);

    IRRender::untrackMetalTexture(texture);

    EXPECT_EQ(IRRender::boundMetalTexture(3), nullptr);
    EXPECT_EQ(IRRender::boundMetalImageTexture(5), nullptr);
}

// A colour attachment surviving untrackMetalTexture is a freed handle that
// createRenderEncoder retains via setTexture: and dereferences for the
// viewport, so the scrub has to reach the render-target pair too (#2808).
TEST(MetalStickyUntrack, ClearsRenderTargetColorAttachment) {
    auto *color = sentinelTexture(0x2001);
    auto *depth = sentinelTexture(0x2002);
    IRRender::bindMetalFramebufferRenderTarget(
        color,
        depth,
        MTL::PixelFormatRGBA8Unorm,
        MTL::PixelFormatDepth32Float
    );
    ASSERT_EQ(IRRender::metalCurrentColorTexture(), color);
    ASSERT_FALSE(IRRender::metalUsesDefaultRenderTarget());

    IRRender::untrackMetalTexture(color);

    EXPECT_EQ(IRRender::metalCurrentColorTexture(), nullptr);
    EXPECT_EQ(IRRender::metalCurrentDepthTexture(), nullptr);
    // Falling back to the default target rather than nulling in place is the
    // point: a null colour attachment with useDefaultRenderTarget_ still false
    // makes createRenderEncoder return nullptr for every later pass, which
    // renders nothing with no crash and no log.
    EXPECT_TRUE(IRRender::metalUsesDefaultRenderTarget());

    IRRender::bindMetalDefaultRenderTarget();
}

// Destroying the depth attachment strands the pair just as destroying the
// colour one does — the encode path reads both.
TEST(MetalStickyUntrack, ClearsRenderTargetDepthAttachment) {
    auto *color = sentinelTexture(0x3001);
    auto *depth = sentinelTexture(0x3002);
    IRRender::bindMetalFramebufferRenderTarget(
        color,
        depth,
        MTL::PixelFormatRGBA8Unorm,
        MTL::PixelFormatDepth32Float
    );
    ASSERT_EQ(IRRender::metalCurrentDepthTexture(), depth);

    IRRender::untrackMetalTexture(depth);

    EXPECT_EQ(IRRender::metalCurrentColorTexture(), nullptr);
    EXPECT_EQ(IRRender::metalCurrentDepthTexture(), nullptr);
    EXPECT_TRUE(IRRender::metalUsesDefaultRenderTarget());

    IRRender::bindMetalDefaultRenderTarget();
}

// Guard against the over-correction: untracking an unrelated texture must not
// steal the render target from whichever framebuffer is actually bound.
TEST(MetalStickyUntrack, LeavesUnrelatedRenderTargetBound) {
    auto *color = sentinelTexture(0x4001);
    auto *depth = sentinelTexture(0x4002);
    auto *unrelated = sentinelTexture(0x4003);
    IRRender::bindMetalFramebufferRenderTarget(
        color,
        depth,
        MTL::PixelFormatRGBA8Unorm,
        MTL::PixelFormatDepth32Float
    );

    IRRender::untrackMetalTexture(unrelated);

    EXPECT_EQ(IRRender::metalCurrentColorTexture(), color);
    EXPECT_EQ(IRRender::metalCurrentDepthTexture(), depth);
    EXPECT_FALSE(IRRender::metalUsesDefaultRenderTarget());

    IRRender::bindMetalDefaultRenderTarget();
}

TEST(MetalStickyUntrack, NullTextureIsANoOp) {
    auto *color = sentinelTexture(0x5001);
    auto *depth = sentinelTexture(0x5002);
    IRRender::bindMetalFramebufferRenderTarget(
        color,
        depth,
        MTL::PixelFormatRGBA8Unorm,
        MTL::PixelFormatDepth32Float
    );

    IRRender::untrackMetalTexture(nullptr);

    EXPECT_EQ(IRRender::metalCurrentColorTexture(), color);
    EXPECT_FALSE(IRRender::metalUsesDefaultRenderTarget());

    IRRender::bindMetalDefaultRenderTarget();
}

#endif // IR_GRAPHICS_METAL

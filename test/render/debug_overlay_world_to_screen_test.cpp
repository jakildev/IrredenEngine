#include <gtest/gtest.h>

#include <irreden/render/debug_overlay_draws.hpp>

#include <type_traits>

// `IRDebug::worldToScreen` is public engine API compiled against from outside
// this tree. It delegates to `IRRender::worldPos3DToMouseScreenPx`, the single
// world-to-window-pixel projection owner, so its value needs the live
// RenderManager and this lock holds only the signature.
TEST(DebugOverlayWorldToScreen, KeepsItsVec3ToVec2Signature) {
    static_assert(
        std::is_same_v<decltype(&IRDebug::worldToScreen), IRMath::vec2 (*)(IRMath::vec3)>
    );
    SUCCEED();
}

TEST(DebugOverlayScreenMarker, HiddenMarkerQueuesNothing) {
    IRDebug::clear();
    IRComponents::C_ScreenSpaceMarker marker;
    IRDebug::drawScreenMarker(marker, IRMath::vec2(800, 600), IRMath::vec2(2));
    EXPECT_TRUE(IRDebug::getScreenTriangles().empty());
    EXPECT_TRUE(IRDebug::getScreenLines().empty());
}

TEST(DebugOverlayScreenMarker, CrossUsesFixedWindowPointAndRadiusAtEachDisplayScale) {
    for (const IRMath::vec2 scale : {IRMath::vec2(1), IRMath::vec2(2), IRMath::vec2(1.5f, 2)}) {
        IRDebug::clear();
        IRComponents::C_ScreenSpaceMarker marker;
        marker.windowPoint_ = IRMath::vec2(31.25f, 70.5f);
        marker.radiusPoints_ = 6;
        marker.color_ = IRMath::Color(255, 208, 64, 255);
        marker.visible_ = true;
        const IRMath::vec2 framebuffer(800, 600);
        IRDebug::drawScreenMarker(marker, framebuffer, scale);
        const auto &triangles = IRDebug::getScreenTriangles();
        ASSERT_EQ(triangles.size(), 4u);
        IRMath::vec2 sum(0), lo(10000), hi(-10000);
        for (const auto &triangle : triangles) {
            EXPECT_FLOAT_EQ(triangle.r, 1.0f);
            EXPECT_FLOAT_EQ(triangle.g, 208.0f / 255.0f);
            EXPECT_FLOAT_EQ(triangle.bColor, 64.0f / 255.0f);
            EXPECT_FLOAT_EQ(triangle.aColor, 1.0f);
            for (const auto point : {triangle.a, triangle.b, triangle.c}) {
                sum += point;
                lo = IRMath::min(lo, point);
                hi = IRMath::max(hi, point);
            }
        }
        const IRMath::vec2 expected(
            marker.windowPoint_.x * scale.x,
            framebuffer.y - marker.windowPoint_.y * scale.y
        );
        EXPECT_NEAR((sum / 12.0f).x, expected.x, 0.0001f);
        EXPECT_NEAR((sum / 12.0f).y, expected.y, 0.0001f);
        EXPECT_EQ(hi - lo, IRMath::vec2(12) * scale);
        EXPECT_EQ(lo, expected - IRMath::vec2(6) * scale);
        EXPECT_EQ(hi, expected + IRMath::vec2(6) * scale);
        IRDebug::clear();
    }
}

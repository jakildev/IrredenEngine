#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>
#include <irreden/ir_platform.hpp>

#include <vector>

namespace {

// kIsoToScreenSign = vec2(1.0f, screenYDirection_) — the Y sign depends on
// the host backend (OpenGL +1 in our convention, Metal -1 or vice versa via
// IRPlatform::kGfx). The tests below derive the expected values from the
// platform sign so they pass identically on both hosts.
constexpr float kSignY = IRPlatform::kGfx.screenYDirection_;

TEST(CameraSubPixelOffsets, IntegerIsoIsZeroResidual) {
    // At integer iso, fract(camIso) is 0; both decomposition outputs must
    // be zero so the camera-anchored content lands exactly on the screen
    // center. Verified at several integer points and zoom levels.
    const std::vector<IRMath::vec2> integerCameras{
        {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {3.0f, 5.0f}, {-2.0f, -7.0f}
    };
    const std::vector<IRMath::vec2> zooms{
        {1.0f, 1.0f}, {2.0f, 2.0f}, {4.0f, 4.0f}
    };
    const std::vector<IRMath::ivec2> scaleFactors{{1, 1}, {2, 2}, {4, 4}};

    for (const IRMath::vec2 cam : integerCameras) {
        for (const IRMath::vec2 zoom : zooms) {
            for (const IRMath::ivec2 sf : scaleFactors) {
                const auto sub = IRMath::cameraSubPixelOffsets(cam, zoom, sf);
                EXPECT_EQ(sub.framebufferGamePxOffset_.x, 0);
                EXPECT_EQ(sub.framebufferGamePxOffset_.y, 0);
                EXPECT_EQ(sub.screenPxResidual_.x, 0);
                EXPECT_EQ(sub.screenPxResidual_.y, 0);
            }
        }
    }
}

TEST(CameraSubPixelOffsets, HalfIsoXAtZoom1ScaleFactor4) {
    // X: fract(0.5) = 0.5, * zoom * 2 = 1.0 game pixels → framebuffer
    // offset = 1 game pixel (X sign positive). screen residual: subGamePx
    // = 0.0, * scaleFactor = 0 screen pixels.
    const auto sub = IRMath::cameraSubPixelOffsets(
        IRMath::vec2{0.5f, 0.0f}, IRMath::vec2{1.0f, 1.0f}, IRMath::ivec2{4, 4}
    );
    EXPECT_EQ(sub.framebufferGamePxOffset_.x, 1);
    EXPECT_EQ(sub.framebufferGamePxOffset_.y, 0);
    EXPECT_EQ(sub.screenPxResidual_.x, 0);
    EXPECT_EQ(sub.screenPxResidual_.y, 0);
}

TEST(CameraSubPixelOffsets, QuarterIsoXAtZoom1ScaleFactor4) {
    // X: fract(0.25) = 0.25, * zoom * 2 = 0.5 game pixels → framebuffer
    // offset = 0 game pixels (floor 0.5 → 0). screen residual: subGamePx
    // = 0.5, * scaleFactor 4 = 2 screen pixels.
    const auto sub = IRMath::cameraSubPixelOffsets(
        IRMath::vec2{0.25f, 0.0f}, IRMath::vec2{1.0f, 1.0f}, IRMath::ivec2{4, 4}
    );
    EXPECT_EQ(sub.framebufferGamePxOffset_.x, 0);
    EXPECT_EQ(sub.framebufferGamePxOffset_.y, 0);
    EXPECT_EQ(sub.screenPxResidual_.x, 2);
    EXPECT_EQ(sub.screenPxResidual_.y, 0);
}

TEST(CameraSubPixelOffsets, ScreenResidualBoundedByScaleFactor) {
    // The screen residual is bounded by `[-scaleFactor, scaleFactor]`
    // per component — the floor() snap guarantees the decomposition fits
    // inside one game pixel (modulo the asymmetric extra step at fract→1
    // when signY is negative, which is exactly compensated by the trixel
    // canvas re-rasterization at the next integer iso).
    const IRMath::ivec2 sf{4, 4};
    const IRMath::vec2 zoom{1.0f, 1.0f};
    for (int step = 0; step < 100; ++step) {
        const float f = static_cast<float>(step) / 100.0f;
        const auto sub = IRMath::cameraSubPixelOffsets({f, f}, zoom, sf);
        EXPECT_GE(sub.screenPxResidual_.x, -sf.x);
        EXPECT_LE(sub.screenPxResidual_.x, sf.x);
        EXPECT_GE(sub.screenPxResidual_.y, -sf.y);
        EXPECT_LE(sub.screenPxResidual_.y, sf.y);
    }
}

TEST(CameraSubPixelOffsets, MonotoneAsCameraAdvances) {
    // As `cameraIso` advances smoothly across one iso pixel along X, the
    // combined screen position (gamePxOffset * scaleFactor + screenPxResidual)
    // must be non-decreasing — this is the anti-vibration property the
    // helper exists to guarantee. A non-monotone step would produce
    // visible +/-1px jitter on a smoothly-moving camera. X uses the
    // unsigned-positive direction (`IRPlatform::kIsoToScreenSign.x == 1`)
    // so the combined value is non-decreasing; the Y direction's signed
    // case is covered by `MatchesLegacyDecomposition`.
    const IRMath::ivec2 sf{4, 4};
    const IRMath::vec2 zoom{1.0f, 1.0f};
    // Strictly less than 1.0: fract(1.0) wraps to 0 (the next iso pixel
    // step), where the trixel canvas re-rasterizes — the combined output
    // wraps with it and is expected to drop back to 0.
    int prevX = std::numeric_limits<int>::min();
    for (int step = 0; step < 200; ++step) {
        const float f = static_cast<float>(step) / 200.0f;
        const auto sub = IRMath::cameraSubPixelOffsets({f, 0.0f}, zoom, sf);
        const int combinedX = sub.framebufferGamePxOffset_.x * sf.x + sub.screenPxResidual_.x;
        EXPECT_GE(combinedX, prevX)
            << "non-monotone at fract=" << f << " combined=" << combinedX << " prev=" << prevX;
        prevX = combinedX;
    }
}

TEST(CameraSubPixelOffsets, MatchesLegacyDecomposition) {
    // Byte-for-byte parity against the pre-refactor inline math that lived
    // in TRIXEL_TO_FRAMEBUFFER + FRAMEBUFFER_TO_SCREEN. Sampled across
    // sub-iso positions at two zoom levels and three scale factors.
    const std::vector<IRMath::vec2> cameraSamples{
        {0.1f, 0.2f}, {0.3f, 0.4f}, {0.5f, 0.5f}, {0.7f, 0.9f}, {0.99f, 0.01f}
    };
    const std::vector<IRMath::vec2> zooms{{1.0f, 1.0f}, {2.0f, 2.0f}};
    const std::vector<IRMath::ivec2> scaleFactors{{1, 1}, {3, 3}, {5, 5}};
    const IRMath::vec2 sign = IRPlatform::kIsoToScreenSign;

    for (const IRMath::vec2 cam : cameraSamples) {
        for (const IRMath::vec2 zoom : zooms) {
            for (const IRMath::ivec2 sf : scaleFactors) {
                const auto sub = IRMath::cameraSubPixelOffsets(cam, zoom, sf);
                const IRMath::vec2 legacyGame = IRMath::floor(
                    IRMath::pos2DIsoToPos2DGameResolution(IRMath::fract(cam), zoom)
                ) * sign;
                const IRMath::vec2 legacyScreen = IRMath::floor(
                    IRMath::fract(IRMath::pos2DIsoToPos2DGameResolution(
                        IRMath::fract(cam), zoom
                    )) * sign * IRMath::vec2(sf)
                );
                EXPECT_EQ(sub.framebufferGamePxOffset_.x, static_cast<int>(legacyGame.x));
                EXPECT_EQ(sub.framebufferGamePxOffset_.y, static_cast<int>(legacyGame.y));
                EXPECT_EQ(sub.screenPxResidual_.x, static_cast<int>(legacyScreen.x));
                EXPECT_EQ(sub.screenPxResidual_.y, static_cast<int>(legacyScreen.y));
            }
        }
    }
    (void)kSignY;
}

} // namespace

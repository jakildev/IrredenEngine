#include <gtest/gtest.h>

#include <irreden/render/occlusion_lag_source.hpp>

// The Hi-Z build gate and the cull's staleness latch with the ECS and GPU
// lifted out (`IRPrefab::OcclusionCull`): COMPUTE_DISTANCE_HIZ builds only on a
// cull-on frame, and a frame whose predecessor built nothing culls nothing.

namespace {

using IRPrefab::OcclusionCull::buildsDistanceHiZ;
using IRPrefab::OcclusionCull::LagSource;
using IRPrefab::OcclusionCull::LagSourceView;

LagSourceView steadyView() {
    return {
        .cameraIso_ = IRMath::vec2(10.0f, -4.0f),
        .zoom_ = IRMath::vec2(2.0f),
        .density_ = 1,
        .cutThreshold_ = 50.0f,
        .backingResized_ = false,
    };
}

TEST(OcclusionLagSource, ChainIsBuiltOnlyWhileTheCullIsOn) {
    EXPECT_FALSE(buildsDistanceHiZ(false));
    EXPECT_TRUE(buildsDistanceHiZ(true));
}

TEST(OcclusionLagSource, FirstEnabledFrameIsStaleThenTheSteadyViewIsNot) {
    LagSource lag;
    EXPECT_TRUE(lag.advance(true, steadyView()));
    EXPECT_FALSE(lag.advance(true, steadyView()));
    EXPECT_FALSE(lag.advance(true, steadyView()));
}

TEST(OcclusionLagSource, FrameAfterAReEnableIsStale) {
    LagSource lag;
    lag.advance(true, steadyView());
    ASSERT_FALSE(lag.advance(true, steadyView()));
    // Each cull-off frame skips the build, so the chain the next cull-on frame
    // would read predates the gap even though the view never moved.
    EXPECT_TRUE(lag.advance(false, {}));
    EXPECT_FALSE(buildsDistanceHiZ(false));
    EXPECT_TRUE(lag.advance(true, steadyView()));
    EXPECT_FALSE(lag.advance(true, steadyView()));
}

TEST(OcclusionLagSource, ViewDiscontinuitiesAreStaleForOneFrame) {
    LagSource lag;
    lag.advance(true, steadyView());
    ASSERT_FALSE(lag.advance(true, steadyView()));

    LagSourceView cut = steadyView();
    cut.cameraIso_ += IRMath::vec2(51.0f, 0.0f);
    EXPECT_TRUE(lag.advance(true, cut));
    EXPECT_FALSE(lag.advance(true, cut));

    LagSourceView pan = cut;
    pan.cameraIso_ += IRMath::vec2(49.0f, 0.0f);
    EXPECT_FALSE(lag.advance(true, pan));

    LagSourceView zoomed = pan;
    zoomed.zoom_ = IRMath::vec2(4.0f);
    EXPECT_TRUE(lag.advance(true, zoomed));

    LagSourceView denser = zoomed;
    denser.density_ = 2;
    EXPECT_TRUE(lag.advance(true, denser));

    LagSourceView resized = denser;
    resized.backingResized_ = true;
    EXPECT_TRUE(lag.advance(true, resized));
    resized.backingResized_ = false;
    EXPECT_FALSE(lag.advance(true, resized));
}

} // namespace

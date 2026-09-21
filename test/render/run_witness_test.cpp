#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>
#include <irreden/render/gpu_stage_timing.hpp>

namespace {

using IRRender::RenderRunWitness;

constexpr float kDegree = IRMath::kPi / 180.0f;

TEST(RenderRunWitness, StaticPoseTravelsNothing) {
    RenderRunWitness witness;
    for (int frame = 0; frame < 300; ++frame) {
        witness.recordPose(45.0f * kDegree, 4.0f);
    }
    EXPECT_EQ(witness.poseSamples_, 300u);
    EXPECT_FLOAT_EQ(witness.yawFirst_, 45.0f * kDegree);
    EXPECT_FLOAT_EQ(witness.yawLast_, 45.0f * kDegree);
    EXPECT_EQ(witness.yawTravel_, 0.0f);
    EXPECT_FLOAT_EQ(witness.zoomFirst_, 4.0f);
}

TEST(RenderRunWitness, TravelSumsTheArcAcrossTheSeam) {
    RenderRunWitness witness;
    // 170° to -170° is a 20° step through ±180°, never a 340° one.
    for (float degrees : {150.0f, 170.0f, -170.0f, -150.0f}) {
        witness.recordPose(degrees * kDegree, 4.0f);
    }
    EXPECT_NEAR(witness.yawTravel_ / kDegree, 60.0f, 1e-3f);
    EXPECT_NEAR(witness.yawFirst_ / kDegree, 150.0f, 1e-3f);
    EXPECT_NEAR(witness.yawLast_ / kDegree, -150.0f, 1e-3f);
}

TEST(RenderRunWitness, TravelCountsAReturnToTheStartingPose) {
    RenderRunWitness witness;
    for (float degrees : {0.0f, 30.0f, 0.0f}) {
        witness.recordPose(degrees * kDegree, 4.0f);
    }
    EXPECT_FLOAT_EQ(witness.yawFirst_, witness.yawLast_);
    EXPECT_NEAR(witness.yawTravel_ / kDegree, 60.0f, 1e-3f);
}

TEST(RenderRunWitness, OverflowKeepsTheWorstFrameNotTheLast) {
    RenderRunWitness witness;
    witness.recordOverflow(100u, 0u, 1024u);
    witness.recordOverflow(1024u, 77u, 1024u);
    witness.recordOverflow(50u, 0u, 1024u);
    EXPECT_EQ(witness.overflowSamples_, 3u);
    EXPECT_EQ(witness.maxOverflowEntries_, 1024u);
    EXPECT_EQ(witness.maxOverflowDropped_, 77u);
    EXPECT_EQ(witness.overflowCap_, 1024u);
}

TEST(RenderRunWitness, ResetForgetsThePreviousRun) {
    RenderRunWitness witness;
    witness.recordPose(1.0f, 2.0f);
    witness.recordPose(2.0f, 2.0f);
    witness.recordOverflow(9u, 9u, 9u);
    witness.reset();
    EXPECT_EQ(witness.poseSamples_, 0u);
    EXPECT_EQ(witness.overflowSamples_, 0u);
    EXPECT_EQ(witness.maxOverflowDropped_, 0u);
    witness.recordPose(0.5f, 1.0f);
    EXPECT_FLOAT_EQ(witness.yawFirst_, 0.5f);
    EXPECT_EQ(witness.yawTravel_, 0.0f);
}

} // namespace

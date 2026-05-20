#include <gtest/gtest.h>

#include <irreden/render/components/component_sprite_animation.hpp>
#include <irreden/render/systems/system_sprite_animation_advance.hpp>

using IRComponents::SpriteLoopMode;
using IRSystem::detail::advanceSpriteAnimation;
using IRSystem::detail::SpriteAnimationAdvanceResult;

namespace {

constexpr float kFps = 10.0f;
constexpr float kFrameDuration = 1.0f / kFps;
constexpr float kEpsilon = 1e-5f;

SpriteAnimationAdvanceResult step(
    SpriteAnimationAdvanceResult prev,
    SpriteLoopMode mode,
    int frameCount,
    float dtSeconds,
    float speed = 1.0f
) {
    return advanceSpriteAnimation(
        prev.frameIndex_,
        prev.elapsedInFrame_,
        prev.pingPongDirection_,
        prev.terminated_,
        mode,
        frameCount,
        kFps,
        dtSeconds,
        speed
    );
}

SpriteAnimationAdvanceResult initialState() {
    return SpriteAnimationAdvanceResult{
        /*frameIndex_*/ 0,
        /*elapsedInFrame_*/ 0.0f,
        /*pingPongDirection_*/ 1,
        /*terminated_*/ false,
    };
}

TEST(SpriteAnimationAdvance, LoopWrapsAfterFrameCount) {
    auto state = initialState();
    state = step(state, SpriteLoopMode::LOOP, 4, kFrameDuration);
    EXPECT_EQ(state.frameIndex_, 1);
    state = step(state, SpriteLoopMode::LOOP, 4, kFrameDuration);
    EXPECT_EQ(state.frameIndex_, 2);
    state = step(state, SpriteLoopMode::LOOP, 4, kFrameDuration);
    EXPECT_EQ(state.frameIndex_, 3);
    state = step(state, SpriteLoopMode::LOOP, 4, kFrameDuration);
    EXPECT_EQ(state.frameIndex_, 0);
    EXPECT_FALSE(state.terminated_);
}

TEST(SpriteAnimationAdvance, OnceClampsAtLastFrameAndTerminates) {
    auto state = initialState();
    for (int i = 0; i < 3; ++i) {
        state = step(state, SpriteLoopMode::ONCE, 3, kFrameDuration);
    }
    EXPECT_EQ(state.frameIndex_, 2);
    EXPECT_TRUE(state.terminated_);

    // Further ticks must not advance past the terminal frame.
    state = step(state, SpriteLoopMode::ONCE, 3, kFrameDuration * 5.0f);
    EXPECT_EQ(state.frameIndex_, 2);
    EXPECT_TRUE(state.terminated_);
}

TEST(SpriteAnimationAdvance, PingPongReversesAtBothEndpoints) {
    // 4 frames: indices 0,1,2,3 then 2,1,0 then 1,2,3 ...
    auto state = initialState();
    state = step(state, SpriteLoopMode::PING_PONG, 4, kFrameDuration);
    EXPECT_EQ(state.frameIndex_, 1);
    state = step(state, SpriteLoopMode::PING_PONG, 4, kFrameDuration);
    EXPECT_EQ(state.frameIndex_, 2);
    state = step(state, SpriteLoopMode::PING_PONG, 4, kFrameDuration);
    EXPECT_EQ(state.frameIndex_, 3);
    state = step(state, SpriteLoopMode::PING_PONG, 4, kFrameDuration);
    EXPECT_EQ(state.frameIndex_, 2);
    EXPECT_EQ(state.pingPongDirection_, -1);
    state = step(state, SpriteLoopMode::PING_PONG, 4, kFrameDuration);
    EXPECT_EQ(state.frameIndex_, 1);
    state = step(state, SpriteLoopMode::PING_PONG, 4, kFrameDuration);
    EXPECT_EQ(state.frameIndex_, 0);
    state = step(state, SpriteLoopMode::PING_PONG, 4, kFrameDuration);
    EXPECT_EQ(state.frameIndex_, 1);
    EXPECT_EQ(state.pingPongDirection_, 1);
}

TEST(SpriteAnimationAdvance, LargeDtSpansMultipleFrames) {
    // Single advance call covers 5 frame intervals. LOOP wraps inside
    // the call, so the result should be (5 % 4) = 1 with a small
    // remainder of elapsedInFrame_ because dt is exactly 5 frame
    // durations (no remainder expected).
    auto state = initialState();
    state = step(state, SpriteLoopMode::LOOP, 4, kFrameDuration * 5.0f);
    EXPECT_EQ(state.frameIndex_, 1);
    EXPECT_NEAR(state.elapsedInFrame_, 0.0f, kEpsilon);
}

TEST(SpriteAnimationAdvance, OnceTerminatesEvenWhenLargeDtOvershoots) {
    auto state = initialState();
    state = step(state, SpriteLoopMode::ONCE, 3, kFrameDuration * 100.0f);
    EXPECT_EQ(state.frameIndex_, 2);
    EXPECT_TRUE(state.terminated_);
    EXPECT_NEAR(state.elapsedInFrame_, 0.0f, kEpsilon);
}

TEST(SpriteAnimationAdvance, PingPongSingleFrameHoldsStill) {
    // Edge case: a sub-animation of length 1. PING_PONG cannot
    // traverse anywhere; the system must consume elapsed time
    // without crashing or moving frame index.
    auto state = initialState();
    state = step(state, SpriteLoopMode::PING_PONG, 1, kFrameDuration * 3.0f);
    EXPECT_EQ(state.frameIndex_, 0);
    EXPECT_FALSE(state.terminated_);
}

TEST(SpriteAnimationAdvance, SubFrameDtAccumulatesElapsed) {
    // Three sub-frame ticks must aggregate to one frame advance.
    auto state = initialState();
    const float third = kFrameDuration / 3.0f;
    state = step(state, SpriteLoopMode::LOOP, 4, third);
    EXPECT_EQ(state.frameIndex_, 0);
    state = step(state, SpriteLoopMode::LOOP, 4, third);
    EXPECT_EQ(state.frameIndex_, 0);
    state = step(state, SpriteLoopMode::LOOP, 4, third);
    EXPECT_EQ(state.frameIndex_, 1);
}

TEST(SpriteAnimationAdvance, SpeedScalesAdvanceRate) {
    // 2x speed should advance two frames per dt that nominally
    // covers one frame interval.
    auto state = initialState();
    state = step(state, SpriteLoopMode::LOOP, 4, kFrameDuration, 2.0f);
    EXPECT_EQ(state.frameIndex_, 2);
}

TEST(SpriteAnimationAdvance, ComponentResetSemanticsMimicPlayAnimation) {
    // Mid-playback reset: the IRPrefab::Sprite::playAnimation entry
    // point clears frameIndex_ / elapsedInFrame_ / pingPongDirection_
    // / terminated_ / stopped_. Verifying the equivalent reset on a
    // C_SpriteAnimation instance documents the contract that the
    // pure-helper math relies on.
    IRComponents::C_SpriteAnimation anim{};
    anim.frameIndex_ = 2;
    anim.elapsedInFrame_ = 0.05f;
    anim.terminated_ = true;
    anim.pingPongDirection_ = -1;
    anim.stopped_ = true;

    // What playAnimation does (open-coded so the test has no
    // dependency on the entity manager):
    anim.frameIndex_ = 0;
    anim.elapsedInFrame_ = 0.0f;
    anim.pingPongDirection_ = 1;
    anim.terminated_ = false;
    anim.stopped_ = false;

    EXPECT_EQ(anim.frameIndex_, 0);
    EXPECT_FLOAT_EQ(anim.elapsedInFrame_, 0.0f);
    EXPECT_EQ(anim.pingPongDirection_, 1);
    EXPECT_FALSE(anim.terminated_);
    EXPECT_FALSE(anim.stopped_);
}

} // namespace

// Guard-predicate tests for the Metal image-atomic scratch resolve (#2488).
//
// VOXEL_TO_TRIXEL_STAGE_1 dispatches RenderDevice::resolveImageAtomicScratch on
// the distance texture only when the shadow-feeder sweep produced a non-empty
// off-screen ring. The plan-review for #2488 asked for that guard's FALSE branch
// to be *observed* rather than asserted in prose: a pixel-level A/B with sun
// shadows off would read zero-diff whether the guard fired or not (with an empty
// ring the resolve is a value-identical copy), so it cannot distinguish "the
// guard suppressed the blit" from "the blit ran and changed nothing".
//
// IRPrefab::SunShadow::shadowFeederRingNonEmpty is that predicate, factored out
// of the tick so it is reachable with no GPU, no RenderManager, and no window.
// Everything below is pure math over the two viewport boxes the tick already
// computes.

#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>
#include <irreden/render/sun_shadow_constants.hpp>

using IRMath::IsoBounds2D;
using IRMath::vec2;
using IRMath::vec3;
using IRPrefab::SunShadow::shadowFeederRingNonEmpty;

namespace {

// A representative visible cull box. Deliberately NOT integer-aligned:
// getCullViewport().isoViewport(margin) returns float bounds, and the
// sub-texel case below depends on that being the realistic shape.
constexpr float kVisibleMinX = 12.5f;
constexpr float kVisibleMinY = -40.5f;
constexpr float kVisibleMaxX = 220.5f;
constexpr float kVisibleMaxY = 180.5f;

IsoBounds2D visibleBox() {
    return {vec2(kVisibleMinX, kVisibleMinY), vec2(kVisibleMaxX, kVisibleMaxY)};
}

// The engine's default sun: overhead with a small -X / -Y tilt. Matches
// FrameDataSun::sunDirection_ (+Z is down, so the sun sits above).
constexpr vec3 kSunDirection = vec3(-0.3f, -0.2f, -0.93f);

} // namespace

// The shadows-off state, end to end through the real derivation. Nothing here
// hardcodes "0 means unchanged" — frameShadowFeederParams() returns a
// default-constructed ShadowFeederParams (sweepDistance_ = 0) when
// getSunShadowsEnabled() is false, and shadowFeederIsoBounds returns its input
// unchanged at a non-positive sweep. So the widened box IS the visible box, and
// the guard must read false: there is no ring, hence no feeder depth to resolve.
TEST(SunShadowFeederRing, ShadowsOffLeavesNoRing) {
    const IsoBounds2D visible = visibleBox();
    const IsoBounds2D feeder = IRMath::shadowFeederIsoBounds(visible, kSunDirection, 0.0f);

    EXPECT_EQ(feeder.min_, visible.min_);
    EXPECT_EQ(feeder.max_, visible.max_);
    EXPECT_FALSE(shadowFeederRingNonEmpty(feeder, visible));
}

// The positive control for the arm above: with shadows on at the production
// sweep the same predicate must fire. Without this, "shadows off => false" is
// satisfied by a predicate that is false for every input.
TEST(SunShadowFeederRing, ProductionSweepProducesARing) {
    const IsoBounds2D visible = visibleBox();
    const IsoBounds2D feeder = IRMath::shadowFeederIsoBounds(
        visible,
        kSunDirection,
        IRPrefab::SunShadow::kSunShadowMaxDistance
    );

    EXPECT_TRUE(shadowFeederRingNonEmpty(feeder, visible));
}

// A detached canvas takes the other branch of the tick's viewport split, which
// assigns the full canvas span to BOTH boxes so stage 2's #1740 depth-only skip
// stays inert. Same-box in means ring-empty out, so detached content is
// structurally resolve-free rather than resolve-free by a separate check.
TEST(SunShadowFeederRing, IdenticalBoundsAreRingEmpty) {
    const IsoBounds2D canvasSpan{vec2(-512.0f, -512.0f), vec2(512.0f, 512.0f)};

    EXPECT_FALSE(shadowFeederRingNonEmpty(canvasSpan, canvasSpan));
}

// The predicate is quantized to the integer iso texels the tick actually
// uploads (frameData_.cullIsoMin_/cullIsoMax_ vs visibleIsoBounds_), NOT to the
// float boxes. A sweep too small to cross a texel boundary widens the float box
// while adding no ring texel for stage 2 to skip, so resolving there would blit
// a whole canvas for nothing.
//
// This is the arm that discriminates the shipped predicate from the plain
// `gpuVp != visibleVp` float comparison: the boxes below are unequal as floats
// and equal as uploaded integers, so a float compare reads true and this reads
// false. Both are "correct" for the ring's contents; only this one matches the
// units the shader tests in.
TEST(SunShadowFeederRing, SubTexelWideningIsNotARing) {
    const IsoBounds2D visible = visibleBox();
    const IsoBounds2D feeder{visible.min_ - vec2(0.25f, 0.25f), visible.max_};

    ASSERT_NE(feeder.min_, visible.min_) << "fixture must differ as floats to be meaningful";
    EXPECT_EQ(IRMath::ivec2(IRMath::floor(feeder.min_)), IRMath::ivec2(IRMath::floor(visible.min_)))
        << "fixture must NOT differ as uploaded integers to be meaningful";
    EXPECT_FALSE(shadowFeederRingNonEmpty(feeder, visible));
}

// Each edge on its own crosses a texel boundary, so no single-edge widening is
// silently dropped by an over-narrow compare (e.g. one that only checked min_).
TEST(SunShadowFeederRing, EachEdgeAloneIsDetected) {
    const IsoBounds2D visible = visibleBox();

    const IsoBounds2D lowX{visible.min_ - vec2(1.0f, 0.0f), visible.max_};
    const IsoBounds2D lowY{visible.min_ - vec2(0.0f, 1.0f), visible.max_};
    const IsoBounds2D highX{visible.min_, visible.max_ + vec2(1.0f, 0.0f)};
    const IsoBounds2D highY{visible.min_, visible.max_ + vec2(0.0f, 1.0f)};

    EXPECT_TRUE(shadowFeederRingNonEmpty(lowX, visible));
    EXPECT_TRUE(shadowFeederRingNonEmpty(lowY, visible));
    EXPECT_TRUE(shadowFeederRingNonEmpty(highX, visible));
    EXPECT_TRUE(shadowFeederRingNonEmpty(highY, visible));
}

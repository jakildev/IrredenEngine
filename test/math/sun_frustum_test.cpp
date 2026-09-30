#include <gtest/gtest.h>
#include <irreden/ir_math.hpp>

namespace {

using IRMath::CardinalIndex;
using IRMath::IsoBounds2D;
using IRMath::ivec2;
using IRMath::vec2;
using IRMath::vec3;

void expectBounds(const IsoBounds2D &actual, vec2 expectedMin, vec2 expectedMax) {
    EXPECT_NEAR(actual.min_.x, expectedMin.x, 1e-5f);
    EXPECT_NEAR(actual.min_.y, expectedMin.y, 1e-5f);
    EXPECT_NEAR(actual.max_.x, expectedMax.x, 1e-5f);
    EXPECT_NEAR(actual.max_.y, expectedMax.y, 1e-5f);
}

TEST(SunFrustumBounds, IntegerIsoSlabProjectsIntoEveryCardinalWorldFrame) {
    const IsoBounds2D expected[] = {
        {{-10.0f, -7.0f}, {8.0f, 11.0f}},
        {{-11.0f, -10.0f}, {7.0f, 8.0f}},
        {{-8.0f, -11.0f}, {10.0f, 7.0f}},
        {{-7.0f, -8.0f}, {11.0f, 10.0f}}
    };
    for (int cardinal = 0; cardinal < 4; ++cardinal) {
        SCOPED_TRACE(cardinal);
        const auto bounds = IRMath::sunFrustumUVBounds(
            ivec2(-6, -12),
            ivec2(12, 18),
            -3.0f,
            9.0f,
            vec3(1, 0, 0),
            vec3(0, 1, 0),
            vec3(0, 0, -1),
            static_cast<CardinalIndex>(cardinal),
            vec3(0)
        );
        expectBounds(bounds, expected[cardinal].min_, expected[cardinal].max_);
    }
}

TEST(SunFrustumBounds, SweepStaysInWorldFrameAcrossCardinals) {
    for (int cardinal = 0; cardinal < 4; ++cardinal) {
        SCOPED_TRACE(cardinal);
        const auto index = static_cast<CardinalIndex>(cardinal);
        const auto unswept = IRMath::sunFrustumUVBounds(
            {-6, -12},
            {12, 18},
            -3,
            9,
            {1, 0, 0},
            {0, 1, 0},
            {0, 0, -1},
            index,
            {0, 0, 0}
        );
        const auto swept = IRMath::sunFrustumUVBounds(
            {-6, -12},
            {12, 18},
            -3,
            9,
            {1, 0, 0},
            {0, 1, 0},
            {0, 0, -1},
            index,
            {3, -5, 7}
        );
        expectBounds(swept, unswept.min_ + vec2(0, -5), unswept.max_ + vec2(3, 0));
    }
}

TEST(SunFrustumBounds, CollapsedSlabAndRectangleRemainAPoint) {
    const auto bounds = IRMath::sunFrustumUVBounds(
        {6, 12},
        {6, 12},
        3,
        3,
        {1, 0, 0},
        {0, 1, 0},
        {0, 0, -1},
        CardinalIndex::k0,
        {0, 0, 0}
    );
    expectBounds(bounds, {-4, 2}, {-4, 2});
}

TEST(SunFrustumBounds, ObliqueBasisMatchesLinearCenterExtentOracle) {
    const vec3 direction = IRMath::normalize(vec3(-0.3f, -0.2f, -0.93f));
    vec3 uHat, vHat;
    IRMath::buildOrthonormalBasis(direction, uHat, vHat);
    const vec3 sweep(4.5f, -3.0f, 7.25f);
    for (int cardinal = 0; cardinal < 4; ++cardinal) {
        SCOPED_TRACE(cardinal);
        const auto index = static_cast<CardinalIndex>(cardinal);
        const auto bounds = IRMath::sunFrustumUVBounds(
            {-12, -24},
            {24, 48},
            -12,
            36,
            uHat,
            vHat,
            direction,
            index,
            sweep
        );
        // The inverse iso basis maps the three independent half extents.
        const vec3 center = IRMath::rotateCardinalZInv(vec3(-1, 5, 8), index) + sweep * 0.5f;
        const vec3 axisX = IRMath::rotateCardinalZInv(vec3(-9, 9, 0), index);
        const vec3 axisY = IRMath::rotateCardinalZInv(vec3(-6, -6, 12), index);
        const vec3 axisDepth(8, 8, 8);
        const vec3 worldDepth = IRMath::rotateCardinalZInv(axisDepth, index);
        vec2 projectedCenter, halfExtent;
        const vec3 basis[] = {uHat, vHat};
        for (int axis = 0; axis < 2; ++axis) {
            projectedCenter[axis] = IRMath::dot(center, basis[axis]);
            halfExtent[axis] = IRMath::abs(IRMath::dot(axisX, basis[axis])) +
                               IRMath::abs(IRMath::dot(axisY, basis[axis])) +
                               IRMath::abs(IRMath::dot(worldDepth, basis[axis])) +
                               IRMath::abs(IRMath::dot(sweep * 0.5f, basis[axis]));
        }
        expectBounds(bounds, projectedCenter - halfExtent, projectedCenter + halfExtent);
    }
}

} // namespace

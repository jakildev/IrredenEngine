#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>

// Contract tests for IRMath::perAxisTrixelCanvasWorstCaseSize, the bounded
// worst-case allocation size for the smooth-camera-Z-yaw per-axis trixel
// canvases.

namespace {

using IRMath::ivec2;
using IRMath::kSqrt2;
using IRMath::perAxisTrixelCanvasWorstCaseSize;

// Helper: ceil(scale * extent) the same way the implementation does.
int ceilScale(int extent, float scale) {
    return static_cast<int>(IRMath::ceil(static_cast<float>(extent) * scale));
}

// The rotated-view bound: what a W x H view needs at the worst residual yaw,
// plus the height headroom and the anchor's half-quantum slack on both sides.
float rotatedViewAcross(ivec2 extent) {
    const float W = static_cast<float>(extent.x);
    const float H = static_cast<float>(extent.y);
    const float span = H <= W ? IRMath::sqrt(W * W + H * H) : (W + H) / kSqrt2;
    return span + 2.0f * (kSqrt2 * IRMath::kPerAxisStoreHeightHeadroom +
                          static_cast<float>(
                              IRMath::kPerAxisStoreAnchorQuantum / 2 + IRMath::kPerAxisStoreEdgePad
                          ));
}

float rotatedViewDown(ivec2 extent) {
    const float W = static_cast<float>(extent.x);
    const float H = static_cast<float>(extent.y);
    const float span = W <= H ? IRMath::sqrt(W * W + H * H) : (W + H) / kSqrt2;
    return span + 2.0f * ((2.0f - kSqrt2) * IRMath::kPerAxisStoreHeightHeadroom +
                          static_cast<float>(
                              IRMath::kPerAxisStoreAnchorQuantum / 2 + IRMath::kPerAxisStoreEdgePad
                          ));
}

// On a wide canvas the density term (2x — the iso canvas packs 2 framebuffer px
// per trixel horizontally) bounds the horizontal axis and the face-shear bound
// W + H (Y/X-face row-1 shear at +-pi/4) bounds the vertical one; the rotated
// view fits inside both.
TEST(PerAxisCanvasSize, WideCanvasMatchesTheDensityAndShearBoundsExactly) {
    const ivec2 size = perAxisTrixelCanvasWorstCaseSize(ivec2{1000, 400}, 1.0f);
    EXPECT_EQ(size.x, ceilScale(1000, 2.0f));
    EXPECT_EQ(size.y, 1000 + 400);
}

// The common 1280x720 main canvas (642 x 722 trixels) keeps the allocation the
// deformation and density bounds alone give it.
TEST(PerAxisCanvasSize, SixteenByNineCanvasIsBoundedByDensityAndShear) {
    const ivec2 size = perAxisTrixelCanvasWorstCaseSize(ivec2{642, 722}, 1.0f);
    EXPECT_EQ(size.x, 2 * 642);
    EXPECT_EQ(size.y, 642 + 722);
}

// A canvas taller than it is wide rotates its long side across the store near
// a quarter turn; 2W no longer holds it and the rotated-view bound takes over.
TEST(PerAxisCanvasSize, TallCanvasGrowsAcrossToHoldTheRotatedView) {
    const ivec2 portrait{272, 962};
    const ivec2 size = perAxisTrixelCanvasWorstCaseSize(portrait, 1.0f);
    EXPECT_GT(size.x, 2 * portrait.x);
    EXPECT_EQ(size.x, static_cast<int>(IRMath::ceil(rotatedViewAcross(portrait))));
    EXPECT_EQ(size.y, portrait.x + portrait.y);
}

// Every bound is a lower bound at every aspect: the deformed footprint
// (sqrt2*W across, W + H down) and the rotated view with its headroom.
TEST(PerAxisCanvasSize, NeverBelowAnyBound) {
    for (const ivec2 extent :
         {ivec2{321, 217}, ivec2{642, 722}, ivec2{272, 962}, ivec2{362, 722}, ivec2{100, 80}}) {
        const ivec2 size = perAxisTrixelCanvasWorstCaseSize(extent, 1.0f);
        EXPECT_GE(size.x, ceilScale(extent.x, kSqrt2));
        EXPECT_GE(size.y, extent.x + extent.y);
        EXPECT_GE(static_cast<float>(size.x), rotatedViewAcross(extent));
        EXPECT_GE(static_cast<float>(size.y), rotatedViewDown(extent));
    }
}

// Bounded above: no unbounded growth as a face goes edge-on. Horizontal is
// capped at the larger of 2x cardinal (density floor at 1px/trixel) and the
// rotated view; vertical at the larger of W + H and the rotated view.
TEST(PerAxisCanvasSize, BoundedAboveByTheLargestBound) {
    for (const ivec2 extent : {ivec2{640, 360}, ivec2{272, 962}, ivec2{100, 80}}) {
        const ivec2 size = perAxisTrixelCanvasWorstCaseSize(extent, 1.0f);
        EXPECT_LE(
            static_cast<float>(size.x),
            IRMath::max(static_cast<float>(ceilScale(extent.x, 2.0f)), rotatedViewAcross(extent)) +
                1.0f
        );
        EXPECT_LE(
            static_cast<float>(size.y),
            IRMath::max(static_cast<float>(extent.x + extent.y), rotatedViewDown(extent)) + 1.0f
        );
        // And never smaller than the cardinal canvas it must be able to represent.
        EXPECT_GE(size.x, extent.x);
        EXPECT_GE(size.y, extent.y);
    }
}

// A coarser minimum trixel size reduces horizontal texels (density term shrinks)
// but leaves vertical unchanged — Y never depends on the density floor.
TEST(PerAxisCanvasSize, CoarserFloorShrinksHorizontalOnly) {
    const ivec2 extent{2000, 600};
    const ivec2 fine = perAxisTrixelCanvasWorstCaseSize(extent, 1.0f);
    const ivec2 coarse = perAxisTrixelCanvasWorstCaseSize(extent, 2.0f);
    EXPECT_LT(coarse.x, fine.x);
    // 2px floor => horizontal density term 2/2 = 1 < sqrt2 => footprint-bound for X.
    EXPECT_EQ(coarse.x, ceilScale(extent.x, kSqrt2));
    EXPECT_EQ(coarse.y, fine.y);
    EXPECT_EQ(coarse.y, extent.x + extent.y);
}

// Sub-pixel floors are clamped to 1 px — a trixel is never subdivided below a
// single framebuffer pixel, so the worst case stays finite.
TEST(PerAxisCanvasSize, SubPixelFloorClampsToOnePixel) {
    const ivec2 extent{128, 96};
    const ivec2 atOne = perAxisTrixelCanvasWorstCaseSize(extent, 1.0f);
    const ivec2 belowOne = perAxisTrixelCanvasWorstCaseSize(extent, 0.25f);
    EXPECT_EQ(belowOne.x, atOne.x);
    EXPECT_EQ(belowOne.y, atOne.y);
}

// Pure function of its inputs — no per-frame variance (the size is allocated
// once and reused; the design forbids per-frame reallocation).
TEST(PerAxisCanvasSize, Deterministic) {
    const ivec2 extent{480, 270};
    EXPECT_EQ(
        perAxisTrixelCanvasWorstCaseSize(extent, 1.0f).x,
        perAxisTrixelCanvasWorstCaseSize(extent, 1.0f).x
    );
    EXPECT_EQ(
        perAxisTrixelCanvasWorstCaseSize(extent, 1.0f).y,
        perAxisTrixelCanvasWorstCaseSize(extent, 1.0f).y
    );
}

} // namespace

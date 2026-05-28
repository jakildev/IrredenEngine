#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>

// Contract tests for IRMath::perAxisTrixelCanvasWorstCaseSize, the bounded
// worst-case allocation size for the smooth-camera-Z-yaw per-axis trixel
// canvases (#1308; docs/design/per-axis-trixel-canvas-rotation.md
// §"Bounded textures + minimum on-screen trixel size").

namespace {

using IRMath::ivec2;
using IRMath::kSqrt2;
using IRMath::perAxisTrixelCanvasWorstCaseSize;

// Helper: ceil(scale * extent) the same way the implementation does.
int ceilScale(int extent, float scale) {
    return static_cast<int>(IRMath::ceil(static_cast<float>(extent) * scale));
}

// At the default 1px floor the horizontal axis is bounded by the density term
// (2× — the iso canvas packs 2 framebuffer px per trixel horizontally) and the
// vertical axis by the √2 footprint term (1 px per trixel vertically ⇒ no extra
// vertical subdivision).
TEST(PerAxisCanvasSize, DefaultFloorMatchesBoundsExactly) {
    const ivec2 size = perAxisTrixelCanvasWorstCaseSize(ivec2{100, 80}, 1.0f);
    EXPECT_EQ(size.x, ceilScale(100, 2.0f));    // density-bound horizontally
    EXPECT_EQ(size.y, ceilScale(80, kSqrt2));   // footprint-bound vertically
}

// The √2 footprint growth is always a lower bound per dimension — the
// stretched / rotated axis needs at least this many texels at residual ±π/4.
TEST(PerAxisCanvasSize, NeverBelowSqrt2Footprint) {
    const ivec2 extent{321, 217};
    const ivec2 size = perAxisTrixelCanvasWorstCaseSize(extent, 1.0f);
    EXPECT_GE(size.x, ceilScale(extent.x, kSqrt2));
    EXPECT_GE(size.y, ceilScale(extent.y, kSqrt2));
}

// Bounded above: no unbounded growth as a face goes edge-on. The min-on-screen
// trixel-size floor caps the skinny axis at 2× the cardinal extent horizontally
// and √2× vertically — never more.
TEST(PerAxisCanvasSize, BoundedAboveByTwoTimesCardinal) {
    const ivec2 extent{640, 360};
    const ivec2 size = perAxisTrixelCanvasWorstCaseSize(extent, 1.0f);
    EXPECT_LE(size.x, ceilScale(extent.x, 2.0f));
    EXPECT_LE(size.y, ceilScale(extent.y, kSqrt2));
    // And never smaller than the cardinal canvas it must be able to represent.
    EXPECT_GE(size.x, extent.x);
    EXPECT_GE(size.y, extent.y);
}

// A coarser minimum trixel size needs fewer texels: the horizontal density
// term shrinks while the √2 footprint floor still holds.
TEST(PerAxisCanvasSize, CoarserFloorShrinksTowardFootprint) {
    const ivec2 extent{200, 200};
    const ivec2 fine = perAxisTrixelCanvasWorstCaseSize(extent, 1.0f);
    const ivec2 coarse = perAxisTrixelCanvasWorstCaseSize(extent, 2.0f);
    EXPECT_LE(coarse.x, fine.x);
    // 2px floor ⇒ horizontal density term 2/2 = 1 < √2 ⇒ footprint-bound.
    EXPECT_EQ(coarse.x, ceilScale(extent.x, kSqrt2));
    EXPECT_EQ(coarse.y, ceilScale(extent.y, kSqrt2));
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

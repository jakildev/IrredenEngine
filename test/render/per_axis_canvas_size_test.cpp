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
// vertical axis by the face-shear bound W + H (Y/X-face row-1 shear at ±π/4
// folds the horizontal span into the vertical footprint).
TEST(PerAxisCanvasSize, DefaultFloorMatchesBoundsExactly) {
    const ivec2 size = perAxisTrixelCanvasWorstCaseSize(ivec2{100, 80}, 1.0f);
    EXPECT_EQ(size.x, ceilScale(100, 2.0f)); // density-bound horizontally
    EXPECT_EQ(size.y, 100 + 80);             // face-shear bound: W + H vertically
}

// The face-shear bound W + H is always a lower bound for Y (and √2·W for X) —
// the per-axis canvas is never smaller than the worst-case deformed footprint.
TEST(PerAxisCanvasSize, NeverBelowWorstCaseFootprint) {
    const ivec2 extent{321, 217};
    const ivec2 size = perAxisTrixelCanvasWorstCaseSize(extent, 1.0f);
    EXPECT_GE(size.x, ceilScale(extent.x, kSqrt2)); // horizontal: √2·W
    EXPECT_GE(size.y, extent.x + extent.y);         // vertical: W + H (face-shear)
}

// Bounded above: no unbounded growth as a face goes edge-on. Horizontal is
// capped at 2× cardinal (density floor at 1px/trixel). Vertical is capped at
// W + H (face-shear bound from Y/X-face row-1 at ±π/4) — never more.
TEST(PerAxisCanvasSize, BoundedAboveByWorstCaseFootprint) {
    const ivec2 extent{640, 360};
    const ivec2 size = perAxisTrixelCanvasWorstCaseSize(extent, 1.0f);
    EXPECT_LE(size.x, ceilScale(extent.x, 2.0f));
    EXPECT_LE(size.y, extent.x + extent.y); // face-shear bound: W + H
    // And never smaller than the cardinal canvas it must be able to represent.
    EXPECT_GE(size.x, extent.x);
    EXPECT_GE(size.y, extent.y);
}

// A coarser minimum trixel size reduces horizontal texels (density term shrinks)
// but leaves vertical unchanged — Y is always dominated by the face-shear bound
// W + H, which is independent of the density floor.
TEST(PerAxisCanvasSize, CoarserFloorShrinksHorizontalOnly) {
    const ivec2 extent{200, 200};
    const ivec2 fine = perAxisTrixelCanvasWorstCaseSize(extent, 1.0f);
    const ivec2 coarse = perAxisTrixelCanvasWorstCaseSize(extent, 2.0f);
    EXPECT_LE(coarse.x, fine.x);
    // 2px floor ⇒ horizontal density term 2/2 = 1 < √2 ⇒ footprint-bound for X.
    EXPECT_EQ(coarse.x, ceilScale(extent.x, kSqrt2));
    // Y is face-shear-bound (W + H) regardless of the density floor.
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

#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>

// Contract tests for IRMath::perAxisTrixelCanvasWorstCaseSize, the bounded
// worst-case allocation size for the smooth-camera-Z-yaw per-axis trixel
// canvases (#1308; docs/design/per-axis-trixel-canvas-rotation.md
// §"Bounded textures + minimum on-screen trixel size").

namespace {

using IRMath::ivec2;
using IRMath::kSqrt2;
using IRMath::perAxisSubdivisionCap;
using IRMath::perAxisTrixelCanvasWorstCaseSize;
using IRMath::vec2;

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

// --- IRMath::perAxisSubdivisionCap (#1431 clip cap + #1469 depth-range term) ---
//
// The cap bounds the per-axis store's `subPerAxis` so the worst-case on-screen
// face-local cell stays inside the bounded canvas. It accounts for two terms:
// the depth-0 iso-rect spread (#1431) and the additive depth-range term (#1469)
// — a voxel at iso depth d sits at world0(iso) + (d/3)·(1,1,1), adding d/3 to
// both canvas axes, bounded by the canvas-storable depth at the uncapped effSub.

// Always at least 1: the per-axis store must accept a single base cell even when
// the visible extent is enormous (extreme zoom-out ⇒ tiny zoom ⇒ huge iso rect).
TEST(PerAxisSubdivisionCap, NeverBelowOne) {
    EXPECT_GE(perAxisSubdivisionCap(ivec2{480, 270}, vec2{0.01f, 0.01f}, 16), 1);
    EXPECT_GE(perAxisSubdivisionCap(ivec2{64, 64}, vec2{1.0f, 1.0f}, 1), 1);
}

// The depth-range term only ever TIGHTENS the cap: adding the depth allowance
// can never raise the cap above the depth-0-only bound. The depth-0 bound is the
// limit as effSub → ∞ (depthAllowance = canvasHalfY/(√2·effSub) → 0), so a
// finite-effSub cap must not exceed the large-effSub cap.
TEST(PerAxisSubdivisionCap, DepthTermNeverLoosens) {
    const ivec2 extent{480, 270};
    const vec2 zoom{4.0f, 4.0f};
    const int depth0Limit = perAxisSubdivisionCap(extent, zoom, 1 << 20);
    EXPECT_LE(perAxisSubdivisionCap(extent, zoom, 2), depth0Limit);
    EXPECT_LE(perAxisSubdivisionCap(extent, zoom, 8), depth0Limit);
    EXPECT_LE(perAxisSubdivisionCap(extent, zoom, 16), depth0Limit);
}

// Cap is monotonic non-decreasing in effSub: a larger requested density shrinks
// the depth allowance (canvasHalfY/(√2·effSub)), so the cap relaxes upward
// toward the depth-0 bound. This is the #1469 self-consistency property — the
// allowance assumes "as deep as effSub can store," which loosens as effSub rises.
TEST(PerAxisSubdivisionCap, MonotonicNonDecreasingInEffSub) {
    const ivec2 extent{640, 360};
    const vec2 zoom{8.0f, 8.0f};
    int prev = perAxisSubdivisionCap(extent, zoom, 1);
    for (int effSub = 2; effSub <= 16; ++effSub) {
        const int cap = perAxisSubdivisionCap(extent, zoom, effSub);
        EXPECT_GE(cap, prev) << "effSub=" << effSub;
        prev = cap;
    }
}

// Higher zoom ⇒ smaller visible iso extent ⇒ the iso-rect term shrinks while the
// depth allowance (zoom-independent at fixed effSub) is unchanged, so the cap
// relaxes upward. Holding effSub fixed, the cap is non-decreasing in zoom.
TEST(PerAxisSubdivisionCap, HigherZoomLoosensCap) {
    const ivec2 extent{480, 270};
    const int effSub = 16;
    const int capZoom1 = perAxisSubdivisionCap(extent, vec2{1.0f, 1.0f}, effSub);
    const int capZoom4 = perAxisSubdivisionCap(extent, vec2{4.0f, 4.0f}, effSub);
    const int capZoom8 = perAxisSubdivisionCap(extent, vec2{8.0f, 8.0f}, effSub);
    EXPECT_GE(capZoom4, capZoom1);
    EXPECT_GE(capZoom8, capZoom4);
}

// The cap guarantees the depth band it claims fits: a face-local cell at the
// worst-case iso displacement AND depth d/3 = depthAllowance, deformed by the √2
// residual-yaw footprint, must land inside the canvas at subPerAxis = cap. We
// reconstruct the bound the body uses and assert the cell is in-canvas.
TEST(PerAxisSubdivisionCap, ClaimedDepthBandFitsCanvas) {
    const ivec2 extent{640, 360};
    const vec2 zoom{4.0f, 4.0f};
    const int effSub = 16;
    const int cap = perAxisSubdivisionCap(extent, zoom, effSub);

    const ivec2 canvasSize = perAxisTrixelCanvasWorstCaseSize(extent, 1.0f);
    const float canvasHalfY = 0.5f * static_cast<float>(canvasSize.y);
    const float isoHalfX = static_cast<float>(extent.x) / (2.0f * zoom.x);
    const float isoHalfY = static_cast<float>(extent.y) / (2.0f * zoom.y);
    const float maxInPlaneXY = 0.5f * isoHalfX + isoHalfY / 6.0f;
    const float maxInPlaneZ = isoHalfY / 3.0f;
    const float depthAllowance = canvasHalfY / (kSqrt2 * static_cast<float>(effSub));
    // Worst-case canvas-Y displacement (cells) of a stored voxel at the claimed
    // depth band, grown by the √2 yaw footprint — must not exceed the half-canvas.
    const float worstCellY =
        static_cast<float>(cap) * kSqrt2 * (IRMath::max(maxInPlaneZ, maxInPlaneXY) + depthAllowance);
    EXPECT_LE(worstCellY, canvasHalfY + 1.0f); // +1 slack for the floor() in cap
}

// Pure function of its inputs — no per-frame variance.
TEST(PerAxisSubdivisionCap, Deterministic) {
    const ivec2 extent{480, 270};
    const vec2 zoom{2.0f, 2.0f};
    EXPECT_EQ(
        perAxisSubdivisionCap(extent, zoom, 8), perAxisSubdivisionCap(extent, zoom, 8)
    );
}

} // namespace

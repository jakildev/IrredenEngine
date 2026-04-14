#include <gtest/gtest.h>
#include <irreden/ir_math.hpp>

// Tests for viewport/resolution/inverse-projection helpers in ir_math.hpp that
// have no existing coverage.  Platform-dependent helpers (pos3DtoPos2DScreen,
// screenDeltaToIsoDelta, isoDeltaToScreenDelta, ortho) are excluded.

namespace {

constexpr float kTolerance = 1e-5f;

// ---------------------------------------------------------------------------
// visibleIsoViewport
// Returns the iso-space rectangle that maps to the visible framebuffer area.
//
// viewCenter = -canvasOriginOffset - floor(cameraIso) + canvasSize / 2
// halfExtent = canvasSize / (zoom * 2)
// result = { viewCenter - halfExtent - margin,
//            viewCenter + halfExtent + margin }
// ---------------------------------------------------------------------------

TEST(VisibleIsoViewportTest, ZeroCameraZeroOffsetZoomOne) {
    // No camera movement, no offset, zoom=1 → full canvas is visible.
    // viewCenter = (0,0) - (0,0) + (50,100) = (50,100)
    // halfExtent = (50,100)
    // min=(0,0), max=(100,200)
    auto b = IRMath::visibleIsoViewport(
        IRMath::vec2(0.0f, 0.0f),
        IRMath::ivec2(0, 0),
        IRMath::ivec2(100, 200)
    );
    EXPECT_NEAR(b.min_.x, 0.0f, kTolerance);
    EXPECT_NEAR(b.min_.y, 0.0f, kTolerance);
    EXPECT_NEAR(b.max_.x, 100.0f, kTolerance);
    EXPECT_NEAR(b.max_.y, 200.0f, kTolerance);
}

TEST(VisibleIsoViewportTest, CameraShiftMovesBoundsOppositely) {
    // Camera shifted by (10,20) → viewCenter shrinks by (10,20).
    // viewCenter = (0,0) - (10,20) + (50,100) = (40,80)
    // halfExtent = (50,100)
    // min=(-10,-20), max=(90,180)
    auto b = IRMath::visibleIsoViewport(
        IRMath::vec2(10.0f, 20.0f),
        IRMath::ivec2(0, 0),
        IRMath::ivec2(100, 200)
    );
    EXPECT_NEAR(b.min_.x, -10.0f, kTolerance);
    EXPECT_NEAR(b.min_.y, -20.0f, kTolerance);
    EXPECT_NEAR(b.max_.x,  90.0f, kTolerance);
    EXPECT_NEAR(b.max_.y, 180.0f, kTolerance);
}

TEST(VisibleIsoViewportTest, ZoomTwoHalvesHalfExtent) {
    // zoom=2 → only the center quarter of the canvas is on screen.
    // viewCenter = (50,100) (same, zero camera/offset)
    // halfExtent = (100,200) / (2*2) = (25,50)
    // min=(25,50), max=(75,150)
    auto b = IRMath::visibleIsoViewport(
        IRMath::vec2(0.0f),
        IRMath::ivec2(0, 0),
        IRMath::ivec2(100, 200),
        IRMath::vec2(2.0f)
    );
    EXPECT_NEAR(b.min_.x,  25.0f, kTolerance);
    EXPECT_NEAR(b.min_.y,  50.0f, kTolerance);
    EXPECT_NEAR(b.max_.x,  75.0f, kTolerance);
    EXPECT_NEAR(b.max_.y, 150.0f, kTolerance);
}

TEST(VisibleIsoViewportTest, MarginExpandsBounds) {
    // margin=5 widens both sides by 5.
    // Base: min=(0,0), max=(100,200) → with margin: min=(-5,-5), max=(105,205)
    auto b = IRMath::visibleIsoViewport(
        IRMath::vec2(0.0f),
        IRMath::ivec2(0, 0),
        IRMath::ivec2(100, 200),
        IRMath::vec2(1.0f),
        5
    );
    EXPECT_NEAR(b.min_.x, -5.0f, kTolerance);
    EXPECT_NEAR(b.min_.y, -5.0f, kTolerance);
    EXPECT_NEAR(b.max_.x, 105.0f, kTolerance);
    EXPECT_NEAR(b.max_.y, 205.0f, kTolerance);
}

TEST(VisibleIsoViewportTest, CanvasOriginOffsetShiftsBounds) {
    // canvasOriginOffset shifts viewCenter negatively.
    // viewCenter = -(50,100) - (0,0) + (50,100) = (0,0)
    // halfExtent = (50,100)
    // min=(-50,-100), max=(50,100)
    auto b = IRMath::visibleIsoViewport(
        IRMath::vec2(0.0f),
        IRMath::ivec2(50, 100),
        IRMath::ivec2(100, 200)
    );
    EXPECT_NEAR(b.min_.x, -50.0f, kTolerance);
    EXPECT_NEAR(b.min_.y, -100.0f, kTolerance);
    EXPECT_NEAR(b.max_.x,  50.0f, kTolerance);
    EXPECT_NEAR(b.max_.y, 100.0f, kTolerance);
}

// ---------------------------------------------------------------------------
// shapeIsoHalfExtent
// Conservative iso-space half-extent for a rectangular-prism voxel shape.
//
// halfSize = voxelSize * 0.5
// extentN = ceil(halfSize.N) + 1
// return vec2(extentX + extentY, extentX + extentY + 2 * extentZ)
// ---------------------------------------------------------------------------

TEST(ShapeIsoHalfExtentTest, UnitVoxel) {
    // halfSize = (0.5, 0.5, 0.5)
    // each extentN = ceil(0.5) + 1 = 2
    // result = (2+2, 2+2+2*2) = (4, 8)
    auto r = IRMath::shapeIsoHalfExtent(IRMath::vec3(1.0f, 1.0f, 1.0f));
    EXPECT_NEAR(r.x, 4.0f, kTolerance);
    EXPECT_NEAR(r.y, 8.0f, kTolerance);
}

TEST(ShapeIsoHalfExtentTest, EvenVoxel) {
    // halfSize = (1, 1, 1) — each extentN = ceil(1) + 1 = 2
    // Same as unit voxel: (4, 8)
    auto r = IRMath::shapeIsoHalfExtent(IRMath::vec3(2.0f, 2.0f, 2.0f));
    EXPECT_NEAR(r.x, 4.0f, kTolerance);
    EXPECT_NEAR(r.y, 8.0f, kTolerance);
}

TEST(ShapeIsoHalfExtentTest, RectangularPrism) {
    // voxelSize = (3, 5, 2)
    // halfSize = (1.5, 2.5, 1.0)
    // extentX = ceil(1.5) + 1 = 3
    // extentY = ceil(2.5) + 1 = 4
    // extentZ = ceil(1.0) + 1 = 2
    // result = (3+4, 3+4+2*2) = (7, 11)
    auto r = IRMath::shapeIsoHalfExtent(IRMath::vec3(3.0f, 5.0f, 2.0f));
    EXPECT_NEAR(r.x, 7.0f, kTolerance);
    EXPECT_NEAR(r.y, 11.0f, kTolerance);
}

TEST(ShapeIsoHalfExtentTest, IsoWidthIsSymmetric) {
    // Swapping X and Y voxel dims should give the same iso width (extentX+extentY
    // is commutative) but changes the height.
    auto a = IRMath::shapeIsoHalfExtent(IRMath::vec3(3.0f, 5.0f, 2.0f));
    auto b = IRMath::shapeIsoHalfExtent(IRMath::vec3(5.0f, 3.0f, 2.0f));
    EXPECT_NEAR(a.x, b.x, kTolerance);
}

// ---------------------------------------------------------------------------
// isEntityOnScreen
// Converts world AABB to iso canvas coords and tests overlap with canvas.
// canvasMin = isoBounds.min + canvasOffsetZ1 + floor(cameraIso)
// canvasMax = isoBounds.max + canvasOffsetZ1 + floor(cameraIso)
// on screen iff canvasMax.x>=0 && canvasMin.x<canvasSize.x
//            && canvasMax.y>=0 && canvasMin.y<canvasSize.y
// ---------------------------------------------------------------------------

TEST(IsEntityOnScreenTest, EntityAtOriginIsOnScreen) {
    // 1x1x1 voxel at origin, offset (50,100), canvas (100,200), camera (0,0).
    // isoBounds of unit voxel at origin: min=(-1,-2), max=(1,2)
    // canvasMin=(49,98), canvasMax=(51,102) — fully inside canvas.
    EXPECT_TRUE(IRMath::isEntityOnScreen(
        IRMath::vec3(0.0f),
        IRMath::ivec3(1, 1, 1),
        IRMath::vec2(0.0f),
        IRMath::ivec2(50, 100),
        IRMath::ivec2(100, 200)
    ));
}

TEST(IsEntityOnScreenTest, EntityFarRightIsOffScreen) {
    // Entity far in the +X direction: iso.x = -x + y goes very negative,
    // pushing canvasMax.x below 0.
    // worldPos=(500,0,0) → iso bounds center ≈ (-500,-500)
    // canvasMax.x = -499 + 50 + 0 = -449 < 0 → off screen.
    EXPECT_FALSE(IRMath::isEntityOnScreen(
        IRMath::vec3(500.0f, 0.0f, 0.0f),
        IRMath::ivec3(1, 1, 1),
        IRMath::vec2(0.0f),
        IRMath::ivec2(50, 100),
        IRMath::ivec2(100, 200)
    ));
}

TEST(IsEntityOnScreenTest, EntityFarBelowIsOffScreen) {
    // Entity with very negative iso.y — far below canvas.
    // worldPos=(0,200,0): iso.y = -0 - 200 + 0 = -200
    // canvasMax.y ≈ -200 + 100 = -100 < 0 → off screen.
    EXPECT_FALSE(IRMath::isEntityOnScreen(
        IRMath::vec3(0.0f, 200.0f, 0.0f),
        IRMath::ivec3(1, 1, 1),
        IRMath::vec2(0.0f),
        IRMath::ivec2(50, 100),
        IRMath::ivec2(100, 200)
    ));
}

TEST(IsEntityOnScreenTest, CameraOffsetBringsEntityOnScreen) {
    // Entity at (0,200,0): iso bounds x∈[199,201], y∈[-202,-198].
    // camera=(-150,100) adds floor(-150)=-150 to canvas x, floor(100)=100 to canvas y:
    //   canvasMin.x = 199+50-150 = 99 < 100 ✓
    //   canvasMax.x = 201+50-150 = 101 >= 0  ✓
    //   canvasMin.y = -202+100+100 = -2 < 200 ✓
    //   canvasMax.y = -198+100+100 =  2 >= 0  ✓  → on screen
    EXPECT_TRUE(IRMath::isEntityOnScreen(
        IRMath::vec3(0.0f, 200.0f, 0.0f),
        IRMath::ivec3(1, 1, 1),
        IRMath::vec2(-150.0f, 100.0f),
        IRMath::ivec2(50, 100),
        IRMath::ivec2(100, 200)
    ));
}

// ---------------------------------------------------------------------------
// calcResolutionWidthFromHeightAndAspectRatio /
// calcResolutionHeightFromWidthAndAspectRatio
//
// Convention: aspectRatio = ivec2(height_component, width_component)
// width  = height * aspectRatio.y / aspectRatio.x
// height = width  * aspectRatio.x / aspectRatio.y
// ---------------------------------------------------------------------------

TEST(AspectRatioTest, SixteenByNineWidthFrom1080p) {
    // 16:9 → pass ivec2(9, 16); 1080 * 16 / 9 = 1920
    EXPECT_EQ(
        IRMath::calcResolutionWidthFromHeightAndAspectRatio(1080, IRMath::ivec2(9, 16)),
        1920
    );
}

TEST(AspectRatioTest, SixteenByNineHeightFrom1920p) {
    // 1920 * 9 / 16 = 1080
    EXPECT_EQ(
        IRMath::calcResolutionHeightFromWidthAndAspectRatio(1920, IRMath::ivec2(9, 16)),
        1080
    );
}

TEST(AspectRatioTest, FourByThreeWidthFrom480p) {
    // 4:3 → ivec2(3, 4); 480 * 4 / 3 = 640
    EXPECT_EQ(
        IRMath::calcResolutionWidthFromHeightAndAspectRatio(480, IRMath::ivec2(3, 4)),
        640
    );
}

TEST(AspectRatioTest, SquareAspectRatioPreservesSize) {
    // 1:1 → width == height
    int w = IRMath::calcResolutionWidthFromHeightAndAspectRatio(200, IRMath::ivec2(1, 1));
    int h = IRMath::calcResolutionHeightFromWidthAndAspectRatio(200, IRMath::ivec2(1, 1));
    EXPECT_EQ(w, 200);
    EXPECT_EQ(h, 200);
}

// ---------------------------------------------------------------------------
// secondsToFrames<FPS>
// Returns ceil(seconds * FPS).
// ---------------------------------------------------------------------------

TEST(SecondsToFramesTest, OneSecondAt60Fps) {
    EXPECT_EQ(IRMath::secondsToFrames<60>(1.0f), 60);
}

TEST(SecondsToFramesTest, HalfSecondAt60Fps) {
    EXPECT_EQ(IRMath::secondsToFrames<60>(0.5f), 30);
}

TEST(SecondsToFramesTest, ZeroSecondsIsZeroFrames) {
    EXPECT_EQ(IRMath::secondsToFrames<60>(0.0f), 0);
}

TEST(SecondsToFramesTest, PartialFrameRoundsUp) {
    // 0.016 * 60 = 0.96 → ceil = 1
    EXPECT_EQ(IRMath::secondsToFrames<60>(0.016f), 1);
}

TEST(SecondsToFramesTest, DifferentFpsTemplate) {
    // 1.0 * 30 = 30
    EXPECT_EQ(IRMath::secondsToFrames<30>(1.0f), 30);
}

// ---------------------------------------------------------------------------
// pos2DScreenToPos2DIso / offsetScreenToIsoTriangles
// Both divide a 2D vector by triangleStepSizeScreen (same formula).
// ---------------------------------------------------------------------------

TEST(ScreenToIsoTest, IdentityStepSize) {
    auto r = IRMath::pos2DScreenToPos2DIso(IRMath::vec2(20.0f, 30.0f), IRMath::vec2(1.0f, 1.0f));
    EXPECT_NEAR(r.x, 20.0f, kTolerance);
    EXPECT_NEAR(r.y, 30.0f, kTolerance);
}

TEST(ScreenToIsoTest, StepSizeDivides) {
    // pos / (2, 1) → x halved, y unchanged
    auto r = IRMath::pos2DScreenToPos2DIso(IRMath::vec2(20.0f, 30.0f), IRMath::vec2(2.0f, 1.0f));
    EXPECT_NEAR(r.x, 10.0f, kTolerance);
    EXPECT_NEAR(r.y, 30.0f, kTolerance);
}

TEST(ScreenToIsoTest, OffsetFunctionSameFormula) {
    IRMath::vec2 offset(40.0f, 60.0f);
    IRMath::vec2 step(4.0f, 2.0f);
    auto a = IRMath::pos2DScreenToPos2DIso(offset, step);
    auto b = IRMath::offsetScreenToIsoTriangles(offset, step);
    EXPECT_NEAR(a.x, b.x, kTolerance);
    EXPECT_NEAR(a.y, b.y, kTolerance);
}

TEST(ScreenToIsoTest, ZeroInputIsZero) {
    auto r = IRMath::pos2DScreenToPos2DIso(IRMath::vec2(0.0f), IRMath::vec2(2.0f, 1.0f));
    EXPECT_NEAR(r.x, 0.0f, kTolerance);
    EXPECT_NEAR(r.y, 0.0f, kTolerance);
}

// ---------------------------------------------------------------------------
// pos2DIsoToPos3DAtZLevelNew / pos2DIsoToPos3DAtZLevelAlt
//
// New: adjusts position by (0, zLevel*2) first, then:
//   x = ceil(-(px+py) / 2.0)
//   y = (px - py) / 2          (integer division)
//   z = zLevel
//
// Alt: same formula with no adjustment (caller provides pre-adjusted pos).
//
// Valid positions (round-trip) have iso.x and iso.y of the same parity.
// ---------------------------------------------------------------------------

TEST(InverseIsoProjectionTest, OriginAtZeroLevel) {
    // posFromOrigin=(0,0), zLevel=0 → (0, 0, 0)
    auto r = IRMath::pos2DIsoToPos3DAtZLevelNew(IRMath::ivec2(0, 0), 0);
    EXPECT_EQ(r.x, 0);
    EXPECT_EQ(r.y, 0);
    EXPECT_EQ(r.z, 0);
}

TEST(InverseIsoProjectionTest, EvenIsoPositionRoundTrips) {
    // iso (2,0) at z=0 → 3D (-1, 1, 0)
    // Verify: pos3DtoPos2DIso(-1,1,0) = ivec2(-(-1)+1, -(-1)-1+0) = (2,0) ✓
    auto r = IRMath::pos2DIsoToPos3DAtZLevelNew(IRMath::ivec2(2, 0), 0);
    EXPECT_EQ(r.x, -1);
    EXPECT_EQ(r.y,  1);
    EXPECT_EQ(r.z,  0);
    auto iso = IRMath::pos3DtoPos2DIso(r);
    EXPECT_EQ(iso.x, 2);
    EXPECT_EQ(iso.y, 0);
}

TEST(InverseIsoProjectionTest, NonZeroZLevelAdjustsOrigin) {
    // posFromOrigin=(0,0), zLevel=2:
    // adjusted = (0,0) - (0,4) = (0,-4)
    // x = ceil(-(0+(-4))/2.0) = ceil(2.0) = 2
    // y = (0-(-4))/2 = 2
    // result = (2, 2, 2)
    auto r = IRMath::pos2DIsoToPos3DAtZLevelNew(IRMath::ivec2(0, 0), 2);
    EXPECT_EQ(r.x, 2);
    EXPECT_EQ(r.y, 2);
    EXPECT_EQ(r.z, 2);
    // Round-trip: iso of (2,2,2) = (-2+2, -2-2+4) = (0,0) ✓
    auto iso = IRMath::pos3DtoPos2DIso(r);
    EXPECT_EQ(iso.x, 0);
    EXPECT_EQ(iso.y, 0);
}

TEST(InverseIsoProjectionTest, AltFormMatchesNewFormWithPreAdjustedPos) {
    // pos2DIsoToPos3DAtZLevelAlt takes position already adjusted (origin relative).
    // For the same input as the zLevel=0 case: position=(2,0)
    auto fromNew = IRMath::pos2DIsoToPos3DAtZLevelNew(IRMath::ivec2(2, 0), 0);
    auto fromAlt = IRMath::pos2DIsoToPos3DAtZLevelAlt(IRMath::ivec2(2, 0), 0);
    EXPECT_EQ(fromNew.x, fromAlt.x);
    EXPECT_EQ(fromNew.y, fromAlt.y);
    EXPECT_EQ(fromNew.z, fromAlt.z);
}

TEST(InverseIsoProjectionTest, AltFormAtNonZeroZLevel) {
    // Alt with pre-adjusted position (0,-4) at zLevel=2 matches New at posFromOrigin=(0,0)
    auto fromNew = IRMath::pos2DIsoToPos3DAtZLevelNew(IRMath::ivec2(0, 0), 2);
    auto fromAlt = IRMath::pos2DIsoToPos3DAtZLevelAlt(IRMath::ivec2(0, -4), 2);
    EXPECT_EQ(fromNew.x, fromAlt.x);
    EXPECT_EQ(fromNew.y, fromAlt.y);
    EXPECT_EQ(fromNew.z, fromAlt.z);
}

} // namespace

#include <gtest/gtest.h>
#include <irreden/ir_math.hpp>

// Tests for constexpr coordinate and canvas helpers in ir_math.hpp that
// have no existing coverage. Excludes platform-dependent helpers
// (pos3DtoPos2DScreen, screenDeltaToIsoDelta).

namespace {

constexpr float kTolerance = 1e-5f;

// ---------------------------------------------------------------------------
// multVecComponents
// Product of all components.
// ---------------------------------------------------------------------------

TEST(MultVecComponentsTest, AllOnes) {
    EXPECT_EQ(IRMath::multVecComponents(IRMath::ivec3(1, 1, 1)), 1);
}

TEST(MultVecComponentsTest, PositiveComponents) {
    EXPECT_EQ(IRMath::multVecComponents(IRMath::ivec3(2, 3, 4)), 24);
}

TEST(MultVecComponentsTest, ZeroComponentGivesZero) {
    EXPECT_EQ(IRMath::multVecComponents(IRMath::ivec3(0, 5, 3)), 0);
}

TEST(MultVecComponentsTest, NegativeComponent) {
    EXPECT_EQ(IRMath::multVecComponents(IRMath::ivec3(-2, 3, 4)), -24);
}

// ---------------------------------------------------------------------------
// roundVec3ToIVec3
// Component-wise round() on vec3 → ivec3.
// ---------------------------------------------------------------------------

TEST(RoundVec3Test, PositiveFractionRoundsDown) {
    auto r = IRMath::roundVec3ToIVec3(IRMath::vec3(1.4f, 2.3f, 3.1f));
    EXPECT_EQ(r.x, 1);
    EXPECT_EQ(r.y, 2);
    EXPECT_EQ(r.z, 3);
}

TEST(RoundVec3Test, PositiveFractionRoundsUp) {
    auto r = IRMath::roundVec3ToIVec3(IRMath::vec3(1.6f, 2.7f, 3.9f));
    EXPECT_EQ(r.x, 2);
    EXPECT_EQ(r.y, 3);
    EXPECT_EQ(r.z, 4);
}

TEST(RoundVec3Test, NegativeFractionRoundsUp) {
    auto r = IRMath::roundVec3ToIVec3(IRMath::vec3(-1.4f, -2.3f, -3.1f));
    EXPECT_EQ(r.x, -1);
    EXPECT_EQ(r.y, -2);
    EXPECT_EQ(r.z, -3);
}

TEST(RoundVec3Test, ExactIntegerUnchanged) {
    auto r = IRMath::roundVec3ToIVec3(IRMath::vec3(5.0f, -3.0f, 0.0f));
    EXPECT_EQ(r.x, 5);
    EXPECT_EQ(r.y, -3);
    EXPECT_EQ(r.z, 0);
}

// ---------------------------------------------------------------------------
// size3DtoOriginOffset2DX1 / Y1 / Z1
// Canvas origin offsets relative to a 3D voxel size.
// X1: ivec2(size.x, size.x + size.y - 1)
// Y1: X1 - ivec2(1, 0)
// Z1: X1 - ivec2(1, 1)
// ---------------------------------------------------------------------------

TEST(OriginOffset2DTest, X1Formula) {
    // size (4, 4, 4): X1 = (4, 4+4-1) = (4, 7)
    auto r = IRMath::size3DtoOriginOffset2DX1(IRMath::uvec3(4, 4, 4));
    EXPECT_EQ(r.x, 4);
    EXPECT_EQ(r.y, 7);
}

TEST(OriginOffset2DTest, X1RectangularSize) {
    // size (3, 5, 2): X1 = (3, 3+5-1) = (3, 7)
    auto r = IRMath::size3DtoOriginOffset2DX1(IRMath::uvec3(3, 5, 2));
    EXPECT_EQ(r.x, 3);
    EXPECT_EQ(r.y, 7);
}

TEST(OriginOffset2DTest, Y1IsX1MinusOneX) {
    IRMath::uvec3 size(4, 4, 4);
    auto x1 = IRMath::size3DtoOriginOffset2DX1(size);
    auto y1 = IRMath::size3DtoOriginOffset2DY1(size);
    EXPECT_EQ(y1.x, x1.x - 1);
    EXPECT_EQ(y1.y, x1.y);
}

TEST(OriginOffset2DTest, Z1IsX1MinusOneXOneY) {
    IRMath::uvec3 size(4, 4, 4);
    auto x1 = IRMath::size3DtoOriginOffset2DX1(size);
    auto z1 = IRMath::size3DtoOriginOffset2DZ1(size);
    EXPECT_EQ(z1.x, x1.x - 1);
    EXPECT_EQ(z1.y, x1.y - 1);
}

// ---------------------------------------------------------------------------
// pos2DIsoToPos2DGameResolution
// Formula: position * zoomLevel * vec2(2, 1)
// Scales iso coordinates to game resolution pixels.
// ---------------------------------------------------------------------------

TEST(Pos2DIsoToGameResTest, ZeroIsZero) {
    auto r = IRMath::pos2DIsoToPos2DGameResolution(IRMath::vec2(0.0f, 0.0f), IRMath::vec2(1.0f, 1.0f));
    EXPECT_NEAR(r.x, 0.0f, kTolerance);
    EXPECT_NEAR(r.y, 0.0f, kTolerance);
}

TEST(Pos2DIsoToGameResTest, XIsDoubledByScale2) {
    auto r = IRMath::pos2DIsoToPos2DGameResolution(IRMath::vec2(10.0f, 20.0f), IRMath::vec2(1.0f, 1.0f));
    EXPECT_NEAR(r.x, 20.0f, kTolerance); // 10 * 1 * 2 = 20
    EXPECT_NEAR(r.y, 20.0f, kTolerance); // 20 * 1 * 1 = 20
}

TEST(Pos2DIsoToGameResTest, ZoomScalesBoth) {
    auto r = IRMath::pos2DIsoToPos2DGameResolution(IRMath::vec2(5.0f, 5.0f), IRMath::vec2(3.0f, 2.0f));
    EXPECT_NEAR(r.x, 30.0f, kTolerance); // 5 * 3 * 2 = 30
    EXPECT_NEAR(r.y, 10.0f, kTolerance); // 5 * 2 * 1 = 10
}

// ---------------------------------------------------------------------------
// size2DIsoToGameResolution
// Formula: size / uvec2(1, 2) * scaleFactor
// Source comment: "Floor division (THIS IS UNTESTED)"
// ---------------------------------------------------------------------------

TEST(Size2DIsoToGameResTest, ScaleOneIsHalfY) {
    // Divides y by 2: (320, 480) / (1, 2) * (1, 1) = (320, 240)
    auto r = IRMath::size2DIsoToGameResolution(IRMath::uvec2(320, 480), IRMath::uvec2(1, 1));
    EXPECT_EQ(r.x, 320u);
    EXPECT_EQ(r.y, 240u);
}

TEST(Size2DIsoToGameResTest, XIsUnchangedByDivision) {
    auto r = IRMath::size2DIsoToGameResolution(IRMath::uvec2(100, 200), IRMath::uvec2(1, 1));
    EXPECT_EQ(r.x, 100u);
    EXPECT_EQ(r.y, 100u);
}

TEST(Size2DIsoToGameResTest, ScaleMultipliesBothComponents) {
    // (100, 200) / (1, 2) = (100, 100), then * (2, 3) = (200, 300)
    auto r = IRMath::size2DIsoToGameResolution(IRMath::uvec2(100, 200), IRMath::uvec2(2, 3));
    EXPECT_EQ(r.x, 200u);
    EXPECT_EQ(r.y, 300u);
}

TEST(Size2DIsoToGameResTest, OddYTruncates) {
    // Floor division: 101 / 2 = 50
    auto r = IRMath::size2DIsoToGameResolution(IRMath::uvec2(10, 101), IRMath::uvec2(1, 1));
    EXPECT_EQ(r.x, 10u);
    EXPECT_EQ(r.y, 50u);
}

// ---------------------------------------------------------------------------
// trixelOriginOffset* — canvas center offsets per voxel face
// All 12 offsets derive from X1 = canvasSize / 2.
// Relative deltas are fixed regardless of canvas size.
// ---------------------------------------------------------------------------

TEST(TrixelOriginOffsetTest, X1IsCenterOfCanvas) {
    IRMath::ivec2 size(100, 200);
    auto x1 = IRMath::trixelOriginOffsetX1(size);
    EXPECT_EQ(x1.x, 50);
    EXPECT_EQ(x1.y, 100);
}

TEST(TrixelOriginOffsetTest, FrontFaceOffsets) {
    IRMath::ivec2 size(100, 200);
    auto x1 = IRMath::trixelOriginOffsetX1(size);
    // X2 = X1 + (0, 1)
    auto x2 = IRMath::trixelOriginOffsetX2(size);
    EXPECT_EQ(x2, x1 + IRMath::ivec2(0, 1));
    // Y1 = X1 + (-1, 0)
    auto y1 = IRMath::trixelOriginOffsetY1(size);
    EXPECT_EQ(y1, x1 + IRMath::ivec2(-1, 0));
    // Y2 = X1 + (-1, 1)
    auto y2 = IRMath::trixelOriginOffsetY2(size);
    EXPECT_EQ(y2, x1 + IRMath::ivec2(-1, 1));
    // Z1 = X1 + (-1, -1)
    auto z1 = IRMath::trixelOriginOffsetZ1(size);
    EXPECT_EQ(z1, x1 + IRMath::ivec2(-1, -1));
    // Z2 = X1 + (0, -1)
    auto z2 = IRMath::trixelOriginOffsetZ2(size);
    EXPECT_EQ(z2, x1 + IRMath::ivec2(0, -1));
}

TEST(TrixelOriginOffsetTest, BackFacesAliasedToFrontFaces) {
    IRMath::ivec2 size(100, 200);
    // X3 = Z1, X4 = Y1, Y3 = Z2, Y4 = X1, Z3 = Y2, Z4 = X2
    EXPECT_EQ(IRMath::trixelOriginOffsetX3(size), IRMath::trixelOriginOffsetZ1(size));
    EXPECT_EQ(IRMath::trixelOriginOffsetX4(size), IRMath::trixelOriginOffsetY1(size));
    EXPECT_EQ(IRMath::trixelOriginOffsetY3(size), IRMath::trixelOriginOffsetZ2(size));
    EXPECT_EQ(IRMath::trixelOriginOffsetY4(size), IRMath::trixelOriginOffsetX1(size));
    EXPECT_EQ(IRMath::trixelOriginOffsetZ3(size), IRMath::trixelOriginOffsetY2(size));
    EXPECT_EQ(IRMath::trixelOriginOffsetZ4(size), IRMath::trixelOriginOffsetX2(size));
}

TEST(TrixelOriginOffsetTest, AllOffsetsScaleWithCanvasSize) {
    // Doubling the canvas size doubles all offsets.
    IRMath::ivec2 small(100, 200);
    IRMath::ivec2 large(200, 400);
    EXPECT_EQ(IRMath::trixelOriginOffsetX1(large), IRMath::trixelOriginOffsetX1(small) * 2);
    // Other offsets have fixed deltas, so they don't simply double — only X1 does.
    // Verify X2 for large vs small: X2(large) = (100, 200) + (0,1), X2(small) = (50, 100) + (0,1)
    auto x2_small = IRMath::trixelOriginOffsetX2(small);
    auto x2_large = IRMath::trixelOriginOffsetX2(large);
    EXPECT_EQ(x2_large.x, 2 * x2_small.x);     // x doubles
    EXPECT_NE(x2_large.y, 2 * x2_small.y);     // y does not double (fixed +1 delta)
}

} // namespace

#include <gtest/gtest.h>
#include <irreden/ir_math.hpp>

// Tests for the pure/constexpr helpers in ir_math.hpp.
// Platform-dependent helpers (pos3DtoPos2DScreen, ortho) are excluded.

namespace {

constexpr float kTolerance = 1e-5f;

// ---------------------------------------------------------------------------
// pos3DtoPos2DIso
// Formula: iso.x = -x + y,  iso.y = -x - y + 2z
// ---------------------------------------------------------------------------

TEST(IsoProjectionTest, OriginProjectsToOrigin) {
    auto result = IRMath::pos3DtoPos2DIso(IRMath::ivec3(0, 0, 0));
    EXPECT_EQ(result.x, 0);
    EXPECT_EQ(result.y, 0);
}

TEST(IsoProjectionTest, PureXAxisStep) {
    // (1,0,0) → iso(-1,-1)
    auto result = IRMath::pos3DtoPos2DIso(IRMath::ivec3(1, 0, 0));
    EXPECT_EQ(result.x, -1);
    EXPECT_EQ(result.y, -1);
}

TEST(IsoProjectionTest, PureYAxisStep) {
    // (0,1,0) → iso(1,-1)
    auto result = IRMath::pos3DtoPos2DIso(IRMath::ivec3(0, 1, 0));
    EXPECT_EQ(result.x, 1);
    EXPECT_EQ(result.y, -1);
}

TEST(IsoProjectionTest, PureZAxisStep) {
    // (0,0,1) → iso(0,2)
    auto result = IRMath::pos3DtoPos2DIso(IRMath::ivec3(0, 0, 1));
    EXPECT_EQ(result.x, 0);
    EXPECT_EQ(result.y, 2);
}

TEST(IsoProjectionTest, DiagonalXYProjectsDown) {
    // (1,1,0) → iso(0,-2)
    auto result = IRMath::pos3DtoPos2DIso(IRMath::ivec3(1, 1, 0));
    EXPECT_EQ(result.x, 0);
    EXPECT_EQ(result.y, -2);
}

TEST(IsoProjectionTest, UniformShiftIsInvisible) {
    // (1,1,1) has the same iso projection as (0,0,0): both map to (0,0)
    auto origin = IRMath::pos3DtoPos2DIso(IRMath::ivec3(0, 0, 0));
    auto shifted = IRMath::pos3DtoPos2DIso(IRMath::ivec3(1, 1, 1));
    EXPECT_EQ(shifted.x, origin.x);
    EXPECT_EQ(shifted.y, origin.y);
}

TEST(IsoProjectionTest, NegativeCoordinates) {
    // (-1,-1,-1) should also project to (0,0)
    auto result = IRMath::pos3DtoPos2DIso(IRMath::ivec3(-1, -1, -1));
    EXPECT_EQ(result.x, 0);
    EXPECT_EQ(result.y, 0);
}

TEST(IsoProjectionTest, ArbitraryPoint) {
    // (2,3,4): iso.x = -2+3=1, iso.y = -2-3+8=3
    auto result = IRMath::pos3DtoPos2DIso(IRMath::ivec3(2, 3, 4));
    EXPECT_EQ(result.x, 1);
    EXPECT_EQ(result.y, 3);
}

TEST(IsoProjectionTest, FloatOverloadMatchesIntOverload) {
    IRMath::vec3 fp(3.0f, 5.0f, 2.0f);
    IRMath::ivec3 ip(3, 5, 2);
    auto fResult = IRMath::pos3DtoPos2DIso(fp);
    auto iResult = IRMath::pos3DtoPos2DIso(ip);
    EXPECT_NEAR(fResult.x, static_cast<float>(iResult.x), kTolerance);
    EXPECT_NEAR(fResult.y, static_cast<float>(iResult.y), kTolerance);
}

// ---------------------------------------------------------------------------
// pos3DtoDistance
// Formula: distance = x + y + z
// ---------------------------------------------------------------------------

TEST(DistanceTest, OriginIsZero) {
    EXPECT_EQ(IRMath::pos3DtoDistance(IRMath::ivec3(0, 0, 0)), 0);
}

TEST(DistanceTest, PositiveComponents) {
    EXPECT_EQ(IRMath::pos3DtoDistance(IRMath::ivec3(1, 2, 3)), 6);
}

TEST(DistanceTest, NegativeComponents) {
    EXPECT_EQ(IRMath::pos3DtoDistance(IRMath::ivec3(-1, 0, 0)), -1);
}

TEST(DistanceTest, UniformShiftIncreasesDistanceByThree) {
    IRMath::ivec3 p(1, 2, 3);
    int base = IRMath::pos3DtoDistance(p);
    int shifted = IRMath::pos3DtoDistance(p + IRMath::ivec3(1));
    EXPECT_EQ(shifted - base, 3);
}

// ---------------------------------------------------------------------------
// isoDepthShift
// Shifts position by (d,d,d), preserving 2D iso projection.
// ---------------------------------------------------------------------------

TEST(IsoDepthShiftTest, ZeroShiftIsIdentity) {
    IRMath::vec3 p(1.0f, 2.0f, 3.0f);
    auto result = IRMath::isoDepthShift(p, 0.0f);
    EXPECT_NEAR(result.x, p.x, kTolerance);
    EXPECT_NEAR(result.y, p.y, kTolerance);
    EXPECT_NEAR(result.z, p.z, kTolerance);
}

TEST(IsoDepthShiftTest, ShiftPreservesIsoProjection) {
    IRMath::vec3 p(2.0f, 3.0f, 1.0f);
    auto shifted = IRMath::isoDepthShift(p, 5.0f);
    auto isoOriginal = IRMath::pos3DtoPos2DIso(p);
    auto isoShifted = IRMath::pos3DtoPos2DIso(shifted);
    EXPECT_NEAR(isoShifted.x, isoOriginal.x, kTolerance);
    EXPECT_NEAR(isoShifted.y, isoOriginal.y, kTolerance);
}

TEST(IsoDepthShiftTest, NegativeShiftDecreaseDepth) {
    IRMath::vec3 p(0.0f, 0.0f, 0.0f);
    auto shifted = IRMath::isoDepthShift(p, -2.0f);
    EXPECT_NEAR(shifted.x, -2.0f, kTolerance);
    EXPECT_NEAR(shifted.y, -2.0f, kTolerance);
    EXPECT_NEAR(shifted.z, -2.0f, kTolerance);
}

// ---------------------------------------------------------------------------
// index2DtoIndex1D / index3DtoIndex1D
// ---------------------------------------------------------------------------

TEST(IndexTest, Index2D_Origin) {
    EXPECT_EQ(IRMath::index2DtoIndex1D(IRMath::ivec2(0, 0), IRMath::ivec2(4, 4)), 0);
}

TEST(IndexTest, Index2D_XStep) {
    // (1,0) in a width-4 grid → 1
    EXPECT_EQ(IRMath::index2DtoIndex1D(IRMath::ivec2(1, 0), IRMath::ivec2(4, 4)), 1);
}

TEST(IndexTest, Index2D_YStep) {
    // (0,1) in a width-4 grid → 4
    EXPECT_EQ(IRMath::index2DtoIndex1D(IRMath::ivec2(0, 1), IRMath::ivec2(4, 4)), 4);
}

TEST(IndexTest, Index2D_LastElement) {
    // (3,3) in a 4x4 grid → 15
    EXPECT_EQ(IRMath::index2DtoIndex1D(IRMath::ivec2(3, 3), IRMath::ivec2(4, 4)), 15);
}

TEST(IndexTest, Index3D_Origin) {
    EXPECT_EQ(IRMath::index3DtoIndex1D(IRMath::ivec3(0, 0, 0), IRMath::ivec3(3, 3, 3)), 0);
}

TEST(IndexTest, Index3D_XStep) {
    EXPECT_EQ(IRMath::index3DtoIndex1D(IRMath::ivec3(1, 0, 0), IRMath::ivec3(3, 3, 3)), 1);
}

TEST(IndexTest, Index3D_YStep) {
    // (0,1,0) in 3x3x3 → 3
    EXPECT_EQ(IRMath::index3DtoIndex1D(IRMath::ivec3(0, 1, 0), IRMath::ivec3(3, 3, 3)), 3);
}

TEST(IndexTest, Index3D_ZStep) {
    // (0,0,1) in 3x3x3 → 9
    EXPECT_EQ(IRMath::index3DtoIndex1D(IRMath::ivec3(0, 0, 1), IRMath::ivec3(3, 3, 3)), 9);
}

TEST(IndexTest, Index3D_LastElement) {
    // (2,2,2) in 3x3x3 → 26
    EXPECT_EQ(IRMath::index3DtoIndex1D(IRMath::ivec3(2, 2, 2), IRMath::ivec3(3, 3, 3)), 26);
}

// ---------------------------------------------------------------------------
// divCeil
// ---------------------------------------------------------------------------

TEST(DivCeilTest, ExactDivision) {
    EXPECT_EQ(IRMath::divCeil(9, 3), 3);
}

TEST(DivCeilTest, RoundsUp) {
    EXPECT_EQ(IRMath::divCeil(10, 3), 4);
}

TEST(DivCeilTest, OneExcessRoundsUp) {
    EXPECT_EQ(IRMath::divCeil(7, 3), 3);
}

TEST(DivCeilTest, ZeroNumerator) {
    EXPECT_EQ(IRMath::divCeil(0, 5), 0);
}

TEST(DivCeilTest, NumeratorSmallerThanDenominator) {
    EXPECT_EQ(IRMath::divCeil(1, 5), 1);
}

// ---------------------------------------------------------------------------
// sumVecComponents
// ---------------------------------------------------------------------------

TEST(SumVecTest, IVec2) {
    EXPECT_EQ(IRMath::sumVecComponents(IRMath::ivec2(3, 4)), 7);
}

TEST(SumVecTest, IVec3) {
    EXPECT_EQ(IRMath::sumVecComponents(IRMath::ivec3(1, 2, 3)), 6);
}

TEST(SumVecTest, ZeroVector) {
    EXPECT_EQ(IRMath::sumVecComponents(IRMath::ivec3(0, 0, 0)), 0);
}

TEST(SumVecTest, NegativeComponents) {
    EXPECT_EQ(IRMath::sumVecComponents(IRMath::ivec2(-3, 5)), 2);
}

// ---------------------------------------------------------------------------
// roundFloatToByte / roundByteToFloat
// ---------------------------------------------------------------------------

TEST(ByteConversionTest, ZeroMapsToZero) {
    EXPECT_EQ(IRMath::roundFloatToByte(0.0f), 0u);
}

TEST(ByteConversionTest, OneMapsTo255) {
    EXPECT_EQ(IRMath::roundFloatToByte(1.0f), 255u);
}

TEST(ByteConversionTest, HalfMapsTo128) {
    // 0.5 * 255 = 127.5, rounds to 128
    EXPECT_EQ(IRMath::roundFloatToByte(0.5f), 128u);
}

TEST(ByteConversionTest, ClampsBelowZero) {
    EXPECT_EQ(IRMath::roundFloatToByte(-1.0f), 0u);
}

TEST(ByteConversionTest, ClampsAboveOne) {
    EXPECT_EQ(IRMath::roundFloatToByte(2.0f), 255u);
}

TEST(ByteConversionTest, RoundByteToFloatZero) {
    EXPECT_NEAR(IRMath::roundByteToFloat(0), 0.0f, kTolerance);
}

TEST(ByteConversionTest, RoundByteToFloat255) {
    EXPECT_NEAR(IRMath::roundByteToFloat(255), 1.0f, kTolerance);
}

TEST(ByteConversionTest, RoundTripIsNearIdentity) {
    // roundByteToFloat(roundFloatToByte(v)) == v for every byte value
    for (int b = 0; b <= 255; ++b) {
        float f = IRMath::roundByteToFloat(static_cast<uint8_t>(b));
        uint8_t back = IRMath::roundFloatToByte(f);
        EXPECT_EQ(back, static_cast<uint8_t>(b)) << "round-trip failed for byte=" << b;
    }
}

// ---------------------------------------------------------------------------
// lerpByte
// ---------------------------------------------------------------------------

TEST(LerpByteTest, AtTZeroReturnsFrom) {
    EXPECT_EQ(IRMath::lerpByte(0, 200, 0.0f), 0u);
}

TEST(LerpByteTest, AtTOneReturnsTo) {
    EXPECT_EQ(IRMath::lerpByte(0, 200, 1.0f), 200u);
}

TEST(LerpByteTest, HalfwayIsInterpolated) {
    // lerpByte(0, 255, 0.5) → roundFloatToByte(0.5) = 128
    uint8_t mid = IRMath::lerpByte(0, 255, 0.5f);
    EXPECT_GE(mid, 127u);
    EXPECT_LE(mid, 128u);
}

TEST(LerpByteTest, ClampsTAtZero) {
    // t < 0 should clamp to t = 0
    EXPECT_EQ(IRMath::lerpByte(10, 200, -1.0f), 10u);
}

TEST(LerpByteTest, ClampsTAtOne) {
    // t > 1 should clamp to t = 1
    EXPECT_EQ(IRMath::lerpByte(10, 200, 2.0f), 200u);
}

// ---------------------------------------------------------------------------
// lerpColor
// ---------------------------------------------------------------------------

TEST(LerpColorTest, AtTZeroReturnsFrom) {
    IRMath::Color a{10, 20, 30, 40};
    IRMath::Color b{100, 150, 200, 250};
    auto result = IRMath::lerpColor(a, b, 0.0f);
    EXPECT_EQ(result.red_, a.red_);
    EXPECT_EQ(result.green_, a.green_);
    EXPECT_EQ(result.blue_, a.blue_);
    EXPECT_EQ(result.alpha_, a.alpha_);
}

TEST(LerpColorTest, AtTOneReturnsTo) {
    IRMath::Color a{10, 20, 30, 40};
    IRMath::Color b{100, 150, 200, 250};
    auto result = IRMath::lerpColor(a, b, 1.0f);
    EXPECT_EQ(result.red_, b.red_);
    EXPECT_EQ(result.green_, b.green_);
    EXPECT_EQ(result.blue_, b.blue_);
    EXPECT_EQ(result.alpha_, b.alpha_);
}

TEST(LerpColorTest, SameColorReturnsIdentity) {
    IRMath::Color c{128, 64, 32, 255};
    auto result = IRMath::lerpColor(c, c, 0.5f);
    EXPECT_EQ(result.red_, c.red_);
    EXPECT_EQ(result.green_, c.green_);
    EXPECT_EQ(result.blue_, c.blue_);
    EXPECT_EQ(result.alpha_, c.alpha_);
}

// ---------------------------------------------------------------------------
// IsoBounds2D
// ---------------------------------------------------------------------------

TEST(IsoBounds2DTest, FromCornersNormalOrder) {
    auto b = IRMath::IsoBounds2D::fromCorners({0.0f, 0.0f}, {4.0f, 6.0f});
    EXPECT_NEAR(b.min_.x, 0.0f, kTolerance);
    EXPECT_NEAR(b.min_.y, 0.0f, kTolerance);
    EXPECT_NEAR(b.max_.x, 4.0f, kTolerance);
    EXPECT_NEAR(b.max_.y, 6.0f, kTolerance);
}

TEST(IsoBounds2DTest, FromCornersSwappedOrder) {
    // fromCorners should normalize min/max regardless of argument order
    auto b = IRMath::IsoBounds2D::fromCorners({4.0f, 6.0f}, {0.0f, 0.0f});
    EXPECT_NEAR(b.min_.x, 0.0f, kTolerance);
    EXPECT_NEAR(b.min_.y, 0.0f, kTolerance);
    EXPECT_NEAR(b.max_.x, 4.0f, kTolerance);
    EXPECT_NEAR(b.max_.y, 6.0f, kTolerance);
}

TEST(IsoBounds2DTest, ContainsInteriorPoint) {
    auto b = IRMath::IsoBounds2D::fromCorners({0.0f, 0.0f}, {10.0f, 10.0f});
    EXPECT_TRUE(b.contains({5.0f, 5.0f}));
}

TEST(IsoBounds2DTest, ContainsBoundaryPoint) {
    auto b = IRMath::IsoBounds2D::fromCorners({0.0f, 0.0f}, {10.0f, 10.0f});
    EXPECT_TRUE(b.contains({0.0f, 0.0f}));
    EXPECT_TRUE(b.contains({10.0f, 10.0f}));
}

TEST(IsoBounds2DTest, DoesNotContainExteriorPoint) {
    auto b = IRMath::IsoBounds2D::fromCorners({0.0f, 0.0f}, {10.0f, 10.0f});
    EXPECT_FALSE(b.contains({11.0f, 5.0f}));
    EXPECT_FALSE(b.contains({5.0f, -1.0f}));
}

TEST(IsoBounds2DTest, Center) {
    auto b = IRMath::IsoBounds2D::fromCorners({0.0f, 0.0f}, {4.0f, 6.0f});
    auto center = b.center();
    EXPECT_NEAR(center.x, 2.0f, kTolerance);
    EXPECT_NEAR(center.y, 3.0f, kTolerance);
}

TEST(IsoBounds2DTest, Extent) {
    auto b = IRMath::IsoBounds2D::fromCorners({1.0f, 2.0f}, {5.0f, 8.0f});
    auto extent = b.extent();
    EXPECT_NEAR(extent.x, 4.0f, kTolerance);
    EXPECT_NEAR(extent.y, 6.0f, kTolerance);
}

// ---------------------------------------------------------------------------
// size3DtoSize2DIso
// Formula: ivec2(x+y, (x+y) + 2z - 1)
// ---------------------------------------------------------------------------

TEST(Size3DToIsoTest, UnitCube) {
    // (1,1,1) → ivec2(2, 3)
    auto result = IRMath::size3DtoSize2DIso(IRMath::ivec3(1, 1, 1));
    EXPECT_EQ(result.x, 2);
    EXPECT_EQ(result.y, 3);
}

TEST(Size3DToIsoTest, TwoCube) {
    // (2,2,2) → ivec2(4, 7)
    auto result = IRMath::size3DtoSize2DIso(IRMath::ivec3(2, 2, 2));
    EXPECT_EQ(result.x, 4);
    EXPECT_EQ(result.y, 7);
}

TEST(Size3DToIsoTest, FlatXYSlab) {
    // (3,2,0) → ivec2(5, 4) ... (3+2) + (0*2) - 1 = 4
    auto result = IRMath::size3DtoSize2DIso(IRMath::ivec3(3, 2, 0));
    EXPECT_EQ(result.x, 5);
    EXPECT_EQ(result.y, 4);
}

// ---------------------------------------------------------------------------
// gameResolutionToSize2DIso (uvec2 overload)
// Formula: resolution / uvec2(2, 1) / scaleFactor
// ---------------------------------------------------------------------------

TEST(GameResolutionToIsoTest, NoScaling) {
    auto result = IRMath::gameResolutionToSize2DIso(IRMath::uvec2(640, 480), IRMath::uvec2(1, 1));
    EXPECT_EQ(result.x, 320u);
    EXPECT_EQ(result.y, 480u);
}

TEST(GameResolutionToIsoTest, WithScaleFactor) {
    auto result = IRMath::gameResolutionToSize2DIso(IRMath::uvec2(640, 480), IRMath::uvec2(2, 2));
    EXPECT_EQ(result.x, 160u);
    EXPECT_EQ(result.y, 240u);
}

// ---------------------------------------------------------------------------
// lerpHSV
// ---------------------------------------------------------------------------

TEST(LerpHSVTest, AtTZeroReturnsFrom) {
    IRMath::ColorHSV a{0.1f, 0.2f, 0.3f, 1.0f};
    IRMath::ColorHSV b{0.9f, 0.8f, 0.7f, 0.5f};
    auto result = IRMath::lerpHSV(a, b, 0.0f);
    EXPECT_NEAR(result.hue_, a.hue_, kTolerance);
    EXPECT_NEAR(result.saturation_, a.saturation_, kTolerance);
    EXPECT_NEAR(result.value_, a.value_, kTolerance);
    EXPECT_NEAR(result.alpha_, a.alpha_, kTolerance);
}

TEST(LerpHSVTest, AtTOneReturnsTo) {
    IRMath::ColorHSV a{0.1f, 0.2f, 0.3f, 1.0f};
    IRMath::ColorHSV b{0.9f, 0.8f, 0.7f, 0.5f};
    auto result = IRMath::lerpHSV(a, b, 1.0f);
    EXPECT_NEAR(result.hue_, b.hue_, kTolerance);
    EXPECT_NEAR(result.saturation_, b.saturation_, kTolerance);
    EXPECT_NEAR(result.value_, b.value_, kTolerance);
    EXPECT_NEAR(result.alpha_, b.alpha_, kTolerance);
}

TEST(LerpHSVTest, HalfwayIsMidpoint) {
    IRMath::ColorHSV a{0.0f, 0.0f, 0.0f, 0.0f};
    IRMath::ColorHSV b{1.0f, 1.0f, 1.0f, 1.0f};
    auto result = IRMath::lerpHSV(a, b, 0.5f);
    EXPECT_NEAR(result.hue_, 0.5f, kTolerance);
    EXPECT_NEAR(result.saturation_, 0.5f, kTolerance);
    EXPECT_NEAR(result.value_, 0.5f, kTolerance);
    EXPECT_NEAR(result.alpha_, 0.5f, kTolerance);
}

TEST(LerpHSVTest, ClampsTBelowZero) {
    IRMath::ColorHSV a{0.2f, 0.2f, 0.2f, 1.0f};
    IRMath::ColorHSV b{0.8f, 0.8f, 0.8f, 0.0f};
    auto result = IRMath::lerpHSV(a, b, -1.0f);
    EXPECT_NEAR(result.hue_, a.hue_, kTolerance);
}

TEST(LerpHSVTest, ClampsTAboveOne) {
    IRMath::ColorHSV a{0.2f, 0.2f, 0.2f, 1.0f};
    IRMath::ColorHSV b{0.8f, 0.8f, 0.8f, 0.0f};
    auto result = IRMath::lerpHSV(a, b, 2.0f);
    EXPECT_NEAR(result.hue_, b.hue_, kTolerance);
}

// ---------------------------------------------------------------------------
// entityIsoBounds
// Computes the tight iso-space AABB of all 8 corners of a world AABB.
// ---------------------------------------------------------------------------

TEST(EntityIsoBoundsTest, UnitVoxelAtOrigin) {
    // A 1x1x1 voxel at (0,0,0) has corners at all 8 combinations of {0,1}^3.
    // Corner (0,0,0) → iso (0,0)
    // Corner (1,0,0) → iso (-1,-1)
    // Corner (0,1,0) → iso (1,-1)
    // Corner (0,0,1) → iso (0,2)
    // Corner (1,1,0) → iso (0,-2)
    // Corner (1,0,1) → iso (-1,1)
    // Corner (0,1,1) → iso (1,1)
    // Corner (1,1,1) → iso (0,0)
    // min = (-1,-2), max = (1,2)
    auto b = IRMath::entityIsoBounds(IRMath::vec3(0.0f), IRMath::ivec3(1, 1, 1));
    EXPECT_NEAR(b.min_.x, -1.0f, kTolerance);
    EXPECT_NEAR(b.min_.y, -2.0f, kTolerance);
    EXPECT_NEAR(b.max_.x,  1.0f, kTolerance);
    EXPECT_NEAR(b.max_.y,  2.0f, kTolerance);
}

TEST(EntityIsoBoundsTest, OriginProjectedInBounds) {
    // The origin itself must be inside the bounds of a voxel placed at origin.
    auto b = IRMath::entityIsoBounds(IRMath::vec3(0.0f), IRMath::ivec3(2, 2, 2));
    auto originIso = IRMath::pos3DtoPos2DIso(IRMath::vec3(0.0f));
    EXPECT_TRUE(b.contains(originIso));
}

TEST(EntityIsoBoundsTest, BoundsEncloseAllCorners) {
    // For a 2x3x1 box at (1,1,0), manually project all 8 corners and verify
    // they all lie within the returned bounds.
    IRMath::vec3 worldPos(1.0f, 1.0f, 0.0f);
    IRMath::ivec3 size(2, 3, 1);
    auto b = IRMath::entityIsoBounds(worldPos, size);
    for (int dx = 0; dx <= 1; ++dx) {
        for (int dy = 0; dy <= 1; ++dy) {
            for (int dz = 0; dz <= 1; ++dz) {
                IRMath::vec3 corner = worldPos + IRMath::vec3(dx * size.x, dy * size.y, dz * size.z);
                auto iso = IRMath::pos3DtoPos2DIso(corner);
                EXPECT_TRUE(b.contains(iso))
                    << "corner (" << dx << "," << dy << "," << dz << ") iso=("
                    << iso.x << "," << iso.y << ") not in bounds";
            }
        }
    }
}

} // namespace

#include <gtest/gtest.h>
#include <irreden/ir_math.hpp>

#include <glm/gtc/constants.hpp>

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
// isoPixelToPos3D
// Inverse of pos3DtoPos2DIso on the depth plane (x+y+z == depth). CPU mirror
// of `isoPixelToPos3D` in `shaders/ir_iso_common.glsl` — the BAKE pass uses
// it to compute the sun-space AABB, so its formula must match exactly.
// ---------------------------------------------------------------------------

TEST(IsoPixelToPos3DTest, OriginRecoversOrigin) {
    auto p = IRMath::isoPixelToPos3D(0, 0, 0.0f);
    EXPECT_NEAR(p.x, 0.0f, kTolerance);
    EXPECT_NEAR(p.y, 0.0f, kTolerance);
    EXPECT_NEAR(p.z, 0.0f, kTolerance);
}

TEST(IsoPixelToPos3DTest, PureZAxisStep) {
    // (0,0,1) → iso(0,2), depth=1; recover (0,0,1)
    auto p = IRMath::isoPixelToPos3D(0, 2, 1.0f);
    EXPECT_NEAR(p.x, 0.0f, kTolerance);
    EXPECT_NEAR(p.y, 0.0f, kTolerance);
    EXPECT_NEAR(p.z, 1.0f, kTolerance);
}

TEST(IsoPixelToPos3DTest, ArbitraryPoint) {
    // (2,3,4) → iso(1,3), depth=9; recover (2,3,4)
    auto p = IRMath::isoPixelToPos3D(1, 3, 9.0f);
    EXPECT_NEAR(p.x, 2.0f, kTolerance);
    EXPECT_NEAR(p.y, 3.0f, kTolerance);
    EXPECT_NEAR(p.z, 4.0f, kTolerance);
}

TEST(IsoPixelToPos3DTest, RoundtripIntegerLattice) {
    // For integer p, the iso projection is integer and the depth
    // sum is exact, so isoPixelToPos3D ∘ pos3DtoPos2DIso recovers p
    // bit-for-bit (within float rounding).
    for (int x = -3; x <= 3; ++x) {
        for (int y = -3; y <= 3; ++y) {
            for (int z = -3; z <= 3; ++z) {
                const IRMath::ivec3 p(x, y, z);
                const auto iso = IRMath::pos3DtoPos2DIso(p);
                const float depth = static_cast<float>(x + y + z);
                const auto recovered = IRMath::isoPixelToPos3D(iso.x, iso.y, depth);
                EXPECT_NEAR(recovered.x, static_cast<float>(x), kTolerance)
                    << "p=(" << x << "," << y << "," << z << ")";
                EXPECT_NEAR(recovered.y, static_cast<float>(y), kTolerance)
                    << "p=(" << x << "," << y << "," << z << ")";
                EXPECT_NEAR(recovered.z, static_cast<float>(z), kTolerance)
                    << "p=(" << x << "," << y << "," << z << ")";
            }
        }
    }
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

// ---------------------------------------------------------------------------
// rotate2D — used by the screen-space residual rotate inverse for picking.
// ---------------------------------------------------------------------------

TEST(Rotate2DTest, ZeroAngleIsIdentity) {
    auto r = IRMath::rotate2D(IRMath::vec2(3.0f, -1.5f), 0.0f);
    EXPECT_NEAR(r.x, 3.0f, kTolerance);
    EXPECT_NEAR(r.y, -1.5f, kTolerance);
}

TEST(Rotate2DTest, HalfPiRotatesXIntoY) {
    // (1, 0) rotated by +π/2 → (0, 1).
    auto r = IRMath::rotate2D(IRMath::vec2(1.0f, 0.0f), glm::half_pi<float>());
    EXPECT_NEAR(r.x, 0.0f, kTolerance);
    EXPECT_NEAR(r.y, 1.0f, kTolerance);
}

TEST(Rotate2DTest, NegativeHalfPiRotatesYIntoX) {
    // (0, 1) rotated by -π/2 → (1, 0).
    auto r = IRMath::rotate2D(IRMath::vec2(0.0f, 1.0f), -glm::half_pi<float>());
    EXPECT_NEAR(r.x, 1.0f, kTolerance);
    EXPECT_NEAR(r.y, 0.0f, kTolerance);
}

TEST(Rotate2DTest, FullCircleRoundTrips) {
    auto r = IRMath::rotate2D(IRMath::vec2(2.0f, 5.0f), glm::two_pi<float>());
    EXPECT_NEAR(r.x, 2.0f, 1e-4f);
    EXPECT_NEAR(r.y, 5.0f, 1e-4f);
}

TEST(Rotate2DTest, ForwardThenInverseIsIdentity) {
    // Inverse of R(α) is R(-α); composition is the picking inverse pattern
    // used by mousePosition2DIsoScreenRender.
    constexpr float angle = 0.37f;
    const IRMath::vec2 v(4.2f, -1.8f);
    auto roundTrip = IRMath::rotate2D(IRMath::rotate2D(v, angle), -angle);
    EXPECT_NEAR(roundTrip.x, v.x, kTolerance);
    EXPECT_NEAR(roundTrip.y, v.y, kTolerance);
}

// ---------------------------------------------------------------------------
// rasterYawCardinalIndex — must agree with the GLSL/Metal helper bitwise so
// the CPU mouseTrixelPositionWorld and the GPU voxel rasterizer compare on
// the same canvas frame.
// ---------------------------------------------------------------------------

TEST(RasterYawCardinalIndexTest, ZeroIsIndexZero) {
    EXPECT_EQ(IRMath::rasterYawCardinalIndex(0.0f), 0);
}

TEST(RasterYawCardinalIndexTest, ExactCardinals) {
    constexpr float halfPi = glm::half_pi<float>();
    EXPECT_EQ(IRMath::rasterYawCardinalIndex(halfPi),       1);
    EXPECT_EQ(IRMath::rasterYawCardinalIndex(2.0f * halfPi), 2);
    EXPECT_EQ(IRMath::rasterYawCardinalIndex(3.0f * halfPi), 3);
    EXPECT_EQ(IRMath::rasterYawCardinalIndex(4.0f * halfPi), 0);
}

TEST(RasterYawCardinalIndexTest, NegativeFoldsIntoRange) {
    constexpr float halfPi = glm::half_pi<float>();
    EXPECT_EQ(IRMath::rasterYawCardinalIndex(-halfPi),       3);
    EXPECT_EQ(IRMath::rasterYawCardinalIndex(-2.0f * halfPi), 2);
    EXPECT_EQ(IRMath::rasterYawCardinalIndex(-3.0f * halfPi), 1);
}

// ---------------------------------------------------------------------------
// rotateCardinalZ / rotateCardinalZInv — must round-trip and match the GLSL
// sign convention (rotateCardinalZ is world→view = R_z(-rasterYaw)).
// ---------------------------------------------------------------------------

TEST(RotateCardinalZTest, IdentityCardinalIsNoOp) {
    IRMath::ivec3 v(3, -2, 5);
    auto r = IRMath::rotateCardinalZ(v, 0);
    EXPECT_EQ(r.x, v.x);
    EXPECT_EQ(r.y, v.y);
    EXPECT_EQ(r.z, v.z);
}

TEST(RotateCardinalZTest, MatchesGLSLPlusXMappings) {
    // World +X (rasterYaw=π/2 → cardinalIndex=1) → view -Y, per the GLSL
    // convention documented in ir_iso_common.glsl: world→view = R_z(-rasterYaw).
    auto r = IRMath::rotateCardinalZ(IRMath::ivec3(1, 0, 0), 1);
    EXPECT_EQ(r.x, 0);
    EXPECT_EQ(r.y, -1);
    EXPECT_EQ(r.z, 0);
}

TEST(RotateCardinalZTest, HalfTurnNegatesXY) {
    // rasterYaw=π → cardinalIndex=2, world→view negates X and Y.
    auto r = IRMath::rotateCardinalZ(IRMath::ivec3(3, -4, 7), 2);
    EXPECT_EQ(r.x, -3);
    EXPECT_EQ(r.y, 4);
    EXPECT_EQ(r.z, 7);
}

TEST(RotateCardinalZTest, InverseRoundTripsAllCardinals) {
    // For every cardinal index, rotateCardinalZInv ∘ rotateCardinalZ must
    // be identity. This is the load-bearing property for the picking
    // inverse: world → canvas → world recovers the original world coords.
    const IRMath::vec3 worlds[] = {
        IRMath::vec3(0.0f, 0.0f, 0.0f),
        IRMath::vec3(1.0f, 0.0f, 0.0f),
        IRMath::vec3(0.0f, 1.0f, 0.0f),
        IRMath::vec3(0.0f, 0.0f, 1.0f),
        IRMath::vec3(2.5f, -3.5f, 1.25f),
        IRMath::vec3(-7.0f, 11.0f, -2.0f),
    };
    for (int idx = 0; idx < 4; ++idx) {
        for (const auto &w : worlds) {
            const IRMath::ivec3 wInt(static_cast<int>(w.x), static_cast<int>(w.y),
                                     static_cast<int>(w.z));
            const IRMath::ivec3 rotated = IRMath::rotateCardinalZ(wInt, idx);
            const IRMath::vec3 back =
                IRMath::rotateCardinalZInv(IRMath::vec3(rotated), idx);
            EXPECT_NEAR(back.x, static_cast<float>(wInt.x), kTolerance) << "idx=" << idx;
            EXPECT_NEAR(back.y, static_cast<float>(wInt.y), kTolerance) << "idx=" << idx;
            EXPECT_NEAR(back.z, static_cast<float>(wInt.z), kTolerance) << "idx=" << idx;
        }
    }
}

// ---------------------------------------------------------------------------
// Picking inverse — composition test that mirrors the on-device flow.
// Forward: world → R_z(rasterYaw) → world_rotated → M → canvas iso.
// Inverse: canvas iso → isoPixelToPos3D(depth) → R_z(-rasterYaw) → world.
// At the original depth this round-trips exactly.
// ---------------------------------------------------------------------------

TEST(PickingInverseTest, RoundTripsAcrossAllCardinals) {
    constexpr float halfPi = glm::half_pi<float>();
    const float rasterYaws[] = {0.0f, halfPi, 2.0f * halfPi, 3.0f * halfPi};
    const IRMath::ivec3 worlds[] = {
        IRMath::ivec3(0, 0, 0),
        IRMath::ivec3(3, 5, 0),
        IRMath::ivec3(-2, 1, 4),
        IRMath::ivec3(7, -3, 2),
    };
    for (float rasterYaw : rasterYaws) {
        const int cardinalIndex = IRMath::rasterYawCardinalIndex(rasterYaw);
        for (const auto &w : worlds) {
            const IRMath::ivec3 wRotated = IRMath::rotateCardinalZ(w, cardinalIndex);
            const IRMath::ivec2 canvasIso = IRMath::pos3DtoPos2DIso(wRotated);
            const float depth = static_cast<float>(IRMath::pos3DtoDistance(wRotated));
            const IRMath::vec3 recoveredRotated =
                IRMath::isoPixelToPos3D(canvasIso.x, canvasIso.y, depth);
            const IRMath::vec3 recoveredWorld =
                IRMath::rotateCardinalZInv(recoveredRotated, cardinalIndex);
            EXPECT_NEAR(recoveredWorld.x, static_cast<float>(w.x), kTolerance)
                << "rasterYaw=" << rasterYaw << " w=(" << w.x << "," << w.y << "," << w.z << ")";
            EXPECT_NEAR(recoveredWorld.y, static_cast<float>(w.y), kTolerance);
            EXPECT_NEAR(recoveredWorld.z, static_cast<float>(w.z), kTolerance);
        }
    }
}

// ---------------------------------------------------------------------------
// rotateCardinalZ vec3 overload — float world positions cardinally
// rotated into the rasterYaw-rotated canvas frame, paralleling the
// ivec3 overload's R_z(-rasterYaw) sign convention.
// ---------------------------------------------------------------------------

TEST(RotateCardinalZVec3Test, MatchesIntegerOverloadOnIntegerValuedFloats) {
    const IRMath::vec3 worlds[] = {
        IRMath::vec3(0.0f, 0.0f, 0.0f),
        IRMath::vec3(3.0f, 5.0f, 0.0f),
        IRMath::vec3(-2.0f, 1.0f, 4.0f),
        IRMath::vec3(7.0f, -3.0f, 2.0f),
    };
    for (int idx = 0; idx < 4; ++idx) {
        for (const auto &w : worlds) {
            const IRMath::vec3 rFloat = IRMath::rotateCardinalZ(w, idx);
            const IRMath::ivec3 rInt = IRMath::rotateCardinalZ(
                IRMath::ivec3(static_cast<int>(w.x), static_cast<int>(w.y),
                              static_cast<int>(w.z)),
                idx);
            EXPECT_NEAR(rFloat.x, static_cast<float>(rInt.x), kTolerance);
            EXPECT_NEAR(rFloat.y, static_cast<float>(rInt.y), kTolerance);
            EXPECT_NEAR(rFloat.z, static_cast<float>(rInt.z), kTolerance);
        }
    }
}

TEST(RotateCardinalZVec3Test, PreservesFractionalCoords) {
    // The float overload exists for non-integer world positions (entity
    // global+offset). For cardinalIndex=1 (rasterYaw=π/2 → world→view
    // = R_z(-π/2)): (x, y, z) → (y, -x, z). Verify exact preservation
    // of fractional values, not just sign behavior.
    const IRMath::vec3 v(2.5f, -3.75f, 1.125f);
    const IRMath::vec3 r = IRMath::rotateCardinalZ(v, 1);
    EXPECT_FLOAT_EQ(r.x, -3.75f);
    EXPECT_FLOAT_EQ(r.y, -2.5f);
    EXPECT_FLOAT_EQ(r.z, 1.125f);
}

TEST(RotateCardinalZVec3Test, InverseRoundTripPerCardinal) {
    const IRMath::vec3 v(1.5f, -2.25f, 0.75f);
    for (int idx = 0; idx < 4; ++idx) {
        const IRMath::vec3 forward = IRMath::rotateCardinalZ(v, idx);
        const IRMath::vec3 back = IRMath::rotateCardinalZInv(forward, idx);
        EXPECT_NEAR(back.x, v.x, kTolerance) << "idx=" << idx;
        EXPECT_NEAR(back.y, v.y, kTolerance) << "idx=" << idx;
        EXPECT_NEAR(back.z, v.z, kTolerance) << "idx=" << idx;
    }
}

// ---------------------------------------------------------------------------
// Hitbox forward-projection chain — proves the math used by
// HITBOX_MOUSE_TEST under non-zero camera yaw is self-consistent.
//
// The system applies: viewPos = R_z(-rasterYaw)·world; then iso project;
// then offset by camera; then scale to game resolution; then place at
// fbResHalf with Y-flip. The cursor is inverse-residual-rotated from
// framebuffer-pixel space into this same canvas-pixel frame.
//
// A hover should fire iff the inverse-rotated mouse falls within the
// hitbox half-extent of the forward-projected entity center. The tests
// below synthesize known mouse + camera-yaw combinations and assert
// the predicted hover state matches what the system would compute.
// ---------------------------------------------------------------------------

namespace detail {

// Mirror of the per-entity tick body in
// `engine/prefabs/irreden/input/systems/system_hitbox_mouse_test.hpp`.
// Returns the canvas-pixel position the entity projects to under the
// captured camera state.
inline IRMath::vec2 forwardProjectEntity(IRMath::vec3 worldPos,
                                          int cardinalIndex,
                                          IRMath::vec2 cameraIso,
                                          IRMath::vec2 cameraZoom,
                                          IRMath::vec2 fbResHalf) {
    const IRMath::vec3 viewPos = IRMath::rotateCardinalZ(worldPos, cardinalIndex);
    const IRMath::vec2 entityIso = IRMath::pos3DtoPos2DIso(viewPos);
    const IRMath::vec2 relativeIso = entityIso - cameraIso;
    const IRMath::vec2 screenOffset =
        IRMath::pos2DIsoToPos2DGameResolution(relativeIso, cameraZoom);
    return IRMath::vec2(fbResHalf.x + screenOffset.x,
                        fbResHalf.y - screenOffset.y);
}

// Build a synthetic mouse position by forward-rotating an entity's
// canvas-pixel center through the residual composite (the inverse of
// the system's beforeTick lifting). Round-tripping through the
// system's inverse must recover the same canvas pixel; that is the
// load-bearing property a yaw-correct hitbox must satisfy.
inline IRMath::vec2 forwardResidualOnCanvasPixel(IRMath::vec2 canvasPixel,
                                                  float residualYaw,
                                                  float effectiveSign,
                                                  IRMath::vec2 fbResHalf) {
    const float forwardAngle = residualYaw * effectiveSign;
    return IRMath::rotate2D(canvasPixel - fbResHalf, forwardAngle) + fbResHalf;
}

inline IRMath::vec2 inverseResidualOnFramebufferPixel(IRMath::vec2 mouseFb,
                                                      float residualYaw,
                                                      float effectiveSign,
                                                      IRMath::vec2 fbResHalf) {
    const float effectiveAngle = -residualYaw * effectiveSign;
    return IRMath::rotate2D(mouseFb - fbResHalf, effectiveAngle) + fbResHalf;
}

} // namespace detail

TEST(HitboxProjectionTest, ResidualRotationRoundTripsAtNonCardinalYaws) {
    // Two non-cardinal residual yaws bracket the [-π/4, π/4] range:
    // π/8 (mid-positive) and the -π/4 boundary that visualYaw=π/4
    // produces (rasterYaw snaps to π/2 by std::round half-away-from-zero,
    // residualYaw = -π/4).
    constexpr float halfPi = glm::half_pi<float>();
    const float residualYaws[] = {halfPi / 4.0f,
                                  -halfPi / 2.0f};
    const IRMath::vec2 fbResHalf(640.0f, 360.0f);
    const IRMath::vec2 canvasPixels[] = {
        IRMath::vec2(640.0f, 360.0f),  // center — invariant under any rotation
        IRMath::vec2(700.0f, 400.0f),
        IRMath::vec2(540.0f, 280.0f),
    };
    // Both screenYDirection signs (+1 Metal/Vulkan, -1 OpenGL) so the test
    // catches a regression on whichever backend isn't compiled in.
    const float effectiveSigns[] = {1.0f, -1.0f};
    for (float residualYaw : residualYaws) {
        for (float sign : effectiveSigns) {
            for (const auto &canvasPixel : canvasPixels) {
                const IRMath::vec2 mouseFb =
                    detail::forwardResidualOnCanvasPixel(
                        canvasPixel, residualYaw, sign, fbResHalf);
                const IRMath::vec2 recovered =
                    detail::inverseResidualOnFramebufferPixel(
                        mouseFb, residualYaw, sign, fbResHalf);
                EXPECT_NEAR(recovered.x, canvasPixel.x, 1e-3f)
                    << "residualYaw=" << residualYaw << " sign=" << sign;
                EXPECT_NEAR(recovered.y, canvasPixel.y, 1e-3f)
                    << "residualYaw=" << residualYaw << " sign=" << sign;
            }
        }
    }
}

TEST(HitboxProjectionTest, HoverFiresAtCardinalYawOnEntityUnderCursor) {
    // visualYaw = π/2 (pure cardinal): rasterYaw = π/2, residualYaw = 0.
    // Entity at world (3, 0, 0) rotates world→view to (0, -3, 0), iso
    // projects to (-3, 3) ... actually iso(0,-3,0) = (0-3+0, 0+3+0) = (-3,3).
    // We synthesize a cursor exactly at the projected center and
    // expect the hitbox half-extent to flip hovered_ on.
    const IRMath::vec3 entityWorld(3.0f, 0.0f, 0.0f);
    const float rasterYaw = glm::half_pi<float>();
    const int cardinalIndex = IRMath::rasterYawCardinalIndex(rasterYaw);
    const IRMath::vec2 cameraIso(0.0f, 0.0f);
    const IRMath::vec2 cameraZoom(1.0f, 1.0f);
    const IRMath::vec2 fbResHalf(640.0f, 360.0f);

    const IRMath::vec2 entityCenter =
        detail::forwardProjectEntity(
            entityWorld, cardinalIndex, cameraIso, cameraZoom, fbResHalf);

    // The cursor lands exactly on the projected center; with any
    // positive halfExtent the hitbox should match.
    const IRMath::vec2 cursorCanvas = entityCenter;
    const IRMath::vec2 halfExtent(8.0f, 8.0f);
    const bool hovered =
        std::abs(cursorCanvas.x - entityCenter.x) <= halfExtent.x &&
        std::abs(cursorCanvas.y - entityCenter.y) <= halfExtent.y;
    EXPECT_TRUE(hovered);

    // Without the cardinal rotation the projected center would land at
    // a different canvas pixel — verify the test is not vacuous. For
    // entity (3,0,0): iso.x = -x+y stays at -3 across both rotations
    // (-3+0 == 0+(-3)), but iso.y flips sign, so y differs.
    const IRMath::vec2 yawZeroCenter =
        detail::forwardProjectEntity(
            entityWorld, /*cardinalIndex=*/0, cameraIso, cameraZoom, fbResHalf);
    EXPECT_NE(entityCenter.y, yawZeroCenter.y);
}

TEST(HitboxProjectionTest, HoverFiresUnderNonCardinalYaw) {
    // Two non-cardinal visualYaw values from the acceptance spec:
    // π/8 (rasterYaw=0, residualYaw=π/8) and π/2 + π/16
    // (rasterYaw=π/2, residualYaw=π/16). Pick a cursor exactly at the
    // forward-projected post-residual framebuffer location and verify
    // the system's inverse-then-compare logic flips hovered_ on.
    constexpr float halfPi = glm::half_pi<float>();
    struct Case {
        float visualYaw_;
        IRMath::vec3 entityWorld_;
    };
    const Case cases[] = {
        {halfPi / 4.0f,                   IRMath::vec3(2.0f, 1.0f, 0.0f)},
        {halfPi + halfPi / 8.0f,           IRMath::vec3(3.0f, -2.0f, 1.0f)},
    };
    const IRMath::vec2 cameraIso(0.0f, 0.0f);
    const IRMath::vec2 cameraZoom(1.0f, 1.0f);
    const IRMath::vec2 fbResHalf(640.0f, 360.0f);
    const IRMath::vec2 halfExtent(6.0f, 6.0f);

    for (float effectiveSign : {1.0f, -1.0f}) {
        for (const auto &c : cases) {
            const float rasterYaw =
                glm::round(c.visualYaw_ / halfPi) * halfPi;
            const float residualYaw = c.visualYaw_ - rasterYaw;
            const int cardinalIndex = IRMath::rasterYawCardinalIndex(rasterYaw);

            // Project entity to its canvas-pixel center.
            const IRMath::vec2 entityCenter =
                detail::forwardProjectEntity(
                    c.entityWorld_, cardinalIndex, cameraIso, cameraZoom,
                    fbResHalf);
            // Forward-rotate that canvas pixel through the residual
            // composite to compute where the user must click on the
            // framebuffer for the hitbox to be "under" the cursor.
            const IRMath::vec2 mouseFb =
                detail::forwardResidualOnCanvasPixel(
                    entityCenter, residualYaw, effectiveSign, fbResHalf);
            // Inverse-rotate the cursor as the system's beforeTick does.
            const IRMath::vec2 mouseCanvas =
                detail::inverseResidualOnFramebufferPixel(
                    mouseFb, residualYaw, effectiveSign, fbResHalf);
            const bool hovered =
                std::abs(mouseCanvas.x - entityCenter.x) <= halfExtent.x &&
                std::abs(mouseCanvas.y - entityCenter.y) <= halfExtent.y;
            EXPECT_TRUE(hovered)
                << "visualYaw=" << c.visualYaw_ << " sign=" << effectiveSign;
        }
    }
}

TEST(HitboxProjectionTest, HoverDoesNotFireOutsideHalfExtentUnderYaw) {
    // Mouse outside the hitbox half-extent in canvas-pixel space must
    // not flip hovered_ even after the inverse-residual rotation. Tests
    // that the inverse helper isn't accidentally widening the hit area
    // (e.g. via wrong sign on screenYDirection).
    constexpr float halfPi = glm::half_pi<float>();
    const float visualYaw = halfPi / 4.0f;  // residualYaw = π/8, raster=0
    const float rasterYaw = glm::round(visualYaw / halfPi) * halfPi;
    const float residualYaw = visualYaw - rasterYaw;
    const int cardinalIndex = IRMath::rasterYawCardinalIndex(rasterYaw);
    const IRMath::vec2 cameraIso(0.0f, 0.0f);
    const IRMath::vec2 cameraZoom(1.0f, 1.0f);
    const IRMath::vec2 fbResHalf(640.0f, 360.0f);
    const IRMath::vec3 entityWorld(2.0f, 1.0f, 0.0f);
    const IRMath::vec2 halfExtent(4.0f, 4.0f);

    const IRMath::vec2 entityCenter =
        detail::forwardProjectEntity(
            entityWorld, cardinalIndex, cameraIso, cameraZoom, fbResHalf);

    // Place the cursor at a canvas pixel 20 units away — well outside
    // the half-extent. Forward-rotate it to fb-space, inverse-rotate
    // it back, and verify the comparison still falls outside.
    const IRMath::vec2 farCanvas = entityCenter + IRMath::vec2(20.0f, 20.0f);
    for (float effectiveSign : {1.0f, -1.0f}) {
        const IRMath::vec2 mouseFb =
            detail::forwardResidualOnCanvasPixel(
                farCanvas, residualYaw, effectiveSign, fbResHalf);
        const IRMath::vec2 mouseCanvas =
            detail::inverseResidualOnFramebufferPixel(
                mouseFb, residualYaw, effectiveSign, fbResHalf);
        const bool hovered =
            std::abs(mouseCanvas.x - entityCenter.x) <= halfExtent.x &&
            std::abs(mouseCanvas.y - entityCenter.y) <= halfExtent.y;
        EXPECT_FALSE(hovered) << "sign=" << effectiveSign;
    }
}

} // namespace

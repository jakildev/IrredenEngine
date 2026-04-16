#include <gtest/gtest.h>
#include <irreden/ir_math.hpp>

// Tests for calculatePartnerTriangleIndex<FaceType> and roundVec(vec2)
// in ir_math.hpp with no existing coverage.

namespace {

// ---------------------------------------------------------------------------
// calculatePartnerTriangleIndex<X_FACE>
//
// Each trixel triangle has a "partner" that together form an isometric
// diamond. The partner is always adjacent in exactly one axis by ±1.
//
// X_FACE: direction is purely vertical (y-axis).
//   even sum(x,y) → partner at (x, y-1)
//   odd  sum(x,y) → partner at (x, y+1)
// ---------------------------------------------------------------------------

TEST(PartnerTriangleXFaceTest, EvenSumMovesDown) {
    // (2,2): sum=4 (even) → partner=(2,1)
    auto p = IRMath::calculatePartnerTriangleIndex<IRMath::FaceType::X_FACE>(
        IRMath::ivec2(2, 2));
    EXPECT_EQ(p.x, 2);
    EXPECT_EQ(p.y, 1);
}

TEST(PartnerTriangleXFaceTest, OddSumMovesUp) {
    // (2,3): sum=5 (odd) → partner=(2,4)
    auto p = IRMath::calculatePartnerTriangleIndex<IRMath::FaceType::X_FACE>(
        IRMath::ivec2(2, 3));
    EXPECT_EQ(p.x, 2);
    EXPECT_EQ(p.y, 4);
}

TEST(PartnerTriangleXFaceTest, XCoordinateUnchanged) {
    for (int x = 0; x <= 5; ++x) {
        auto p = IRMath::calculatePartnerTriangleIndex<IRMath::FaceType::X_FACE>(
            IRMath::ivec2(x, 0));
        EXPECT_EQ(p.x, x) << "x changed for input x=" << x;
    }
}

TEST(PartnerTriangleXFaceTest, InverseProperty) {
    for (int x = 0; x <= 4; ++x) {
        for (int y = 0; y <= 4; ++y) {
            IRMath::ivec2 idx(x, y);
            auto once = IRMath::calculatePartnerTriangleIndex<IRMath::FaceType::X_FACE>(idx);
            auto twice = IRMath::calculatePartnerTriangleIndex<IRMath::FaceType::X_FACE>(once);
            EXPECT_EQ(twice.x, idx.x) << "inverse failed at (" << x << "," << y << ")";
            EXPECT_EQ(twice.y, idx.y) << "inverse failed at (" << x << "," << y << ")";
        }
    }
}

// ---------------------------------------------------------------------------
// calculatePartnerTriangleIndex<Y_FACE>
//
// Y_FACE: same vertical axis as X_FACE but parity is OPPOSITE.
//   even sum(x,y) → partner at (x, y+1)
//   odd  sum(x,y) → partner at (x, y-1)
// ---------------------------------------------------------------------------

TEST(PartnerTriangleYFaceTest, EvenSumMovesUp) {
    // (2,2): sum=4 (even) → partner=(2,3)
    auto p = IRMath::calculatePartnerTriangleIndex<IRMath::FaceType::Y_FACE>(
        IRMath::ivec2(2, 2));
    EXPECT_EQ(p.x, 2);
    EXPECT_EQ(p.y, 3);
}

TEST(PartnerTriangleYFaceTest, OddSumMovesDown) {
    // (2,3): sum=5 (odd) → partner=(2,2)
    auto p = IRMath::calculatePartnerTriangleIndex<IRMath::FaceType::Y_FACE>(
        IRMath::ivec2(2, 3));
    EXPECT_EQ(p.x, 2);
    EXPECT_EQ(p.y, 2);
}

TEST(PartnerTriangleYFaceTest, OppositeDirectionToXFace) {
    // For the same even-sum index, Y_FACE and X_FACE move in opposite directions.
    IRMath::ivec2 idx(4, 2); // sum=6 (even)
    auto xPartner = IRMath::calculatePartnerTriangleIndex<IRMath::FaceType::X_FACE>(idx);
    auto yPartner = IRMath::calculatePartnerTriangleIndex<IRMath::FaceType::Y_FACE>(idx);
    EXPECT_EQ(xPartner.y, idx.y - 1);
    EXPECT_EQ(yPartner.y, idx.y + 1);
    EXPECT_EQ(xPartner.x, yPartner.x); // x is unchanged by both
}

TEST(PartnerTriangleYFaceTest, InverseProperty) {
    for (int x = 0; x <= 4; ++x) {
        for (int y = 0; y <= 4; ++y) {
            IRMath::ivec2 idx(x, y);
            auto once = IRMath::calculatePartnerTriangleIndex<IRMath::FaceType::Y_FACE>(idx);
            auto twice = IRMath::calculatePartnerTriangleIndex<IRMath::FaceType::Y_FACE>(once);
            EXPECT_EQ(twice.x, idx.x) << "inverse failed at (" << x << "," << y << ")";
            EXPECT_EQ(twice.y, idx.y) << "inverse failed at (" << x << "," << y << ")";
        }
    }
}

// ---------------------------------------------------------------------------
// calculatePartnerTriangleIndex<Z_FACE>
//
// Z_FACE: direction is purely horizontal (x-axis).
//   even sum(x,y) → partner at (x-1, y)
//   odd  sum(x,y) → partner at (x+1, y)
// ---------------------------------------------------------------------------

TEST(PartnerTriangleZFaceTest, EvenSumMovesLeft) {
    // (2,2): sum=4 (even) → partner=(1,2)
    auto p = IRMath::calculatePartnerTriangleIndex<IRMath::FaceType::Z_FACE>(
        IRMath::ivec2(2, 2));
    EXPECT_EQ(p.x, 1);
    EXPECT_EQ(p.y, 2);
}

TEST(PartnerTriangleZFaceTest, OddSumMovesRight) {
    // (2,3): sum=5 (odd) → partner=(3,3)
    auto p = IRMath::calculatePartnerTriangleIndex<IRMath::FaceType::Z_FACE>(
        IRMath::ivec2(2, 3));
    EXPECT_EQ(p.x, 3);
    EXPECT_EQ(p.y, 3);
}

TEST(PartnerTriangleZFaceTest, YCoordinateUnchanged) {
    for (int y = 0; y <= 5; ++y) {
        auto p = IRMath::calculatePartnerTriangleIndex<IRMath::FaceType::Z_FACE>(
            IRMath::ivec2(0, y));
        EXPECT_EQ(p.y, y) << "y changed for input y=" << y;
    }
}

TEST(PartnerTriangleZFaceTest, InverseProperty) {
    for (int x = 0; x <= 4; ++x) {
        for (int y = 0; y <= 4; ++y) {
            IRMath::ivec2 idx(x, y);
            auto once = IRMath::calculatePartnerTriangleIndex<IRMath::FaceType::Z_FACE>(idx);
            auto twice = IRMath::calculatePartnerTriangleIndex<IRMath::FaceType::Z_FACE>(once);
            EXPECT_EQ(twice.x, idx.x) << "inverse failed at (" << x << "," << y << ")";
            EXPECT_EQ(twice.y, idx.y) << "inverse failed at (" << x << "," << y << ")";
        }
    }
}

// ---------------------------------------------------------------------------
// roundVec(vec2) → ivec2
// Component-wise round() on vec2, returns ivec2.
// ---------------------------------------------------------------------------

TEST(RoundVecTest, PositiveFractionRoundsToNearest) {
    auto r = IRMath::roundVec(IRMath::vec2(1.4f, 2.6f));
    EXPECT_EQ(r.x, 1);
    EXPECT_EQ(r.y, 3);
}

TEST(RoundVecTest, ExactHalfRoundsToEven) {
    // GLM uses round-half-away-from-zero: 0.5 → 1, -0.5 → -1
    auto r = IRMath::roundVec(IRMath::vec2(0.5f, -0.5f));
    EXPECT_EQ(r.x, 1);
    EXPECT_EQ(r.y, -1);
}

TEST(RoundVecTest, ExactIntegerUnchanged) {
    auto r = IRMath::roundVec(IRMath::vec2(3.0f, -4.0f));
    EXPECT_EQ(r.x, 3);
    EXPECT_EQ(r.y, -4);
}

TEST(RoundVecTest, ZeroIsZero) {
    auto r = IRMath::roundVec(IRMath::vec2(0.0f, 0.0f));
    EXPECT_EQ(r.x, 0);
    EXPECT_EQ(r.y, 0);
}

} // namespace

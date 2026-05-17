#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>
#include <irreden/render/picking.hpp>

// Pins the architect-locked face-normal derivation from #792:
// `sign(largestAbsComponent(worldHitPos − voxelCenter))`. The editor
// (T-211) computes place-adjacent voxel targets as
// `hit.voxelPos_ + hit.faceNormal_`, so the normal must consistently
// point out of the face the ray crossed to enter the cube. Voxels are
// unit cubes centered on integer coords; `worldHitPos − voxelCenter`
// lies inside `[-0.5, 0.5]^3`. The picker's caller can rely on each
// component of the returned normal being in `{-1, 0, +1}` with
// exactly one non-zero axis.

namespace {

using IRMath::ivec3;
using IRMath::vec3;
using IRPrefab::Picking::detail::voxelFaceNormal;

TEST(PickingFaceNormal, PlusXFaceWhenHitOnRightSide) {
    EXPECT_EQ(voxelFaceNormal(vec3(5.4f, 3.0f, 2.0f), ivec3(5, 3, 2)), ivec3(1, 0, 0));
}

TEST(PickingFaceNormal, MinusXFaceWhenHitOnLeftSide) {
    EXPECT_EQ(voxelFaceNormal(vec3(4.6f, 3.0f, 2.0f), ivec3(5, 3, 2)), ivec3(-1, 0, 0));
}

TEST(PickingFaceNormal, PlusYFaceWhenHitOnFrontSide) {
    EXPECT_EQ(voxelFaceNormal(vec3(5.0f, 3.4f, 2.0f), ivec3(5, 3, 2)), ivec3(0, 1, 0));
}

TEST(PickingFaceNormal, MinusYFaceWhenHitOnBackSide) {
    EXPECT_EQ(voxelFaceNormal(vec3(5.0f, 2.6f, 2.0f), ivec3(5, 3, 2)), ivec3(0, -1, 0));
}

TEST(PickingFaceNormal, PlusZFaceWhenHitOnTopSide) {
    EXPECT_EQ(voxelFaceNormal(vec3(5.0f, 3.0f, 2.4f), ivec3(5, 3, 2)), ivec3(0, 0, 1));
}

TEST(PickingFaceNormal, MinusZFaceWhenHitOnBottomSide) {
    EXPECT_EQ(voxelFaceNormal(vec3(5.0f, 3.0f, 1.6f), ivec3(5, 3, 2)), ivec3(0, 0, -1));
}

TEST(PickingFaceNormal, NegativeVoxelCoordPlusXFace) {
    // Mixed-sign coords — confirm the formula doesn't bias toward positive
    // axes when the voxel sits at a negative world coord.
    EXPECT_EQ(voxelFaceNormal(vec3(-2.4f, -1.0f, 4.0f), ivec3(-3, -1, 4)), ivec3(1, 0, 0));
}

TEST(PickingFaceNormal, OriginVoxelMinusZFace) {
    EXPECT_EQ(voxelFaceNormal(vec3(0.0f, 0.0f, -0.4f), ivec3(0, 0, 0)), ivec3(0, 0, -1));
}

TEST(PickingFaceNormal, ExactCenterFavorsXAxisPositive) {
    // Pure center hit: all three component magnitudes equal (zero). The
    // tie-break order (x → y → z) lands on the +X face. Corner hits
    // never arise in practice (ray sample lands strictly inside one cube
    // before the next depth step), so any of the three faces would be a
    // valid place-adjacent direction — this test pins the deterministic
    // choice for regression-detection rather than asserting a semantic.
    EXPECT_EQ(voxelFaceNormal(vec3(5.0f, 3.0f, 2.0f), ivec3(5, 3, 2)), ivec3(1, 0, 0));
}

TEST(PickingFaceNormal, ReturnsSingleNonzeroAxis) {
    // Exhaustively spot-check that the returned normal has exactly one
    // non-zero component — the place-adjacent compute `voxelPos + normal`
    // breaks badly if two axes fire simultaneously.
    const vec3 worldHits[] = {
        vec3(5.49f, 3.40f, 2.30f),  // X dominant
        vec3(5.30f, 3.49f, 2.40f),  // Y dominant
        vec3(5.30f, 3.40f, 2.49f),  // Z dominant
        vec3(4.51f, 2.60f, 1.70f),  // -X dominant
    };
    for (const vec3 &p : worldHits) {
        const ivec3 normal = voxelFaceNormal(p, ivec3(5, 3, 2));
        const int nonzero =
            (normal.x != 0 ? 1 : 0) + (normal.y != 0 ? 1 : 0) + (normal.z != 0 ? 1 : 0);
        EXPECT_EQ(nonzero, 1) << "world=(" << p.x << "," << p.y << "," << p.z << ") normal=("
                              << normal.x << "," << normal.y << "," << normal.z << ")";
    }
}

} // namespace

#include <gtest/gtest.h>

#include <irreden/render/picking.hpp>

namespace {

using IRPrefab::Picking::detail::voxelHitFaceNormal;

TEST(PickingFaceNormal, PositiveXFace) {
    EXPECT_EQ(
        voxelHitFaceNormal(IRMath::vec3(0.4f, 0.1f, -0.2f)),
        IRMath::ivec3(1, 0, 0)
    );
}

TEST(PickingFaceNormal, NegativeXFace) {
    EXPECT_EQ(
        voxelHitFaceNormal(IRMath::vec3(-0.45f, 0.0f, 0.1f)),
        IRMath::ivec3(-1, 0, 0)
    );
}

TEST(PickingFaceNormal, PositiveZFace) {
    EXPECT_EQ(
        voxelHitFaceNormal(IRMath::vec3(0.1f, -0.2f, 0.4f)),
        IRMath::ivec3(0, 0, 1)
    );
}

TEST(PickingFaceNormal, NegativeYFace) {
    EXPECT_EQ(
        voxelHitFaceNormal(IRMath::vec3(0.2f, -0.45f, 0.3f)),
        IRMath::ivec3(0, -1, 0)
    );
}

TEST(PickingFaceNormal, CornerTieBreaksToFirstMaxAxis) {
    // Tie between x and y at 0.4: x-axis wins by iteration order. Tie
    // between x and z at 0.4: x wins. Documents the deterministic
    // tie-break so callers know what to expect at corners.
    EXPECT_EQ(
        voxelHitFaceNormal(IRMath::vec3(0.4f, 0.4f, 0.1f)),
        IRMath::ivec3(1, 0, 0)
    );
}

TEST(PickingFaceNormal, ZeroDeltaProducesPositiveXFallback) {
    // Degenerate input (ray hit exactly the voxel center). Picks +X
    // deterministically — caller should treat zero-delta as "no
    // preferred face" if it matters.
    EXPECT_EQ(
        voxelHitFaceNormal(IRMath::vec3(0.0f, 0.0f, 0.0f)),
        IRMath::ivec3(1, 0, 0)
    );
}

} // namespace

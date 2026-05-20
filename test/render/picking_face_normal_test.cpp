#include <gtest/gtest.h>

#include <irreden/render/picking.hpp>

namespace {

using IRPrefab::Picking::detail::voxelHitFaceNormal;

TEST(PickingFaceNormal, PositiveXFace) {
    EXPECT_EQ(voxelHitFaceNormal(IRMath::vec3(0.4f, 0.1f, -0.2f)), IRMath::ivec3(1, 0, 0));
}

TEST(PickingFaceNormal, NegativeXFace) {
    EXPECT_EQ(voxelHitFaceNormal(IRMath::vec3(-0.45f, 0.0f, 0.1f)), IRMath::ivec3(-1, 0, 0));
}

TEST(PickingFaceNormal, PositiveZFace) {
    EXPECT_EQ(voxelHitFaceNormal(IRMath::vec3(0.1f, -0.2f, 0.4f)), IRMath::ivec3(0, 0, 1));
}

TEST(PickingFaceNormal, NegativeYFace) {
    EXPECT_EQ(voxelHitFaceNormal(IRMath::vec3(0.2f, -0.45f, 0.3f)), IRMath::ivec3(0, -1, 0));
}

TEST(PickingFaceNormal, CornerTieBreaksToFirstMaxAxis) {
    // Tie between x and y at 0.4: x-axis wins by iteration order. Tie
    // between x and z at 0.4: x wins. Documents the deterministic
    // tie-break so callers know what to expect at corners.
    EXPECT_EQ(voxelHitFaceNormal(IRMath::vec3(0.4f, 0.4f, 0.1f)), IRMath::ivec3(1, 0, 0));
}

TEST(PickingFaceNormal, OutOfBoundsDeltaPositiveXFace) {
    // Pure-function stress-test: delta (0.6, 0, 0) is outside the
    // [-0.5, 0.5) range the rounding step guarantees in normal flow.
    // Exercises robustness for callers that bypass the rounding path.
    EXPECT_EQ(
        voxelHitFaceNormal(IRMath::vec3(0.6f, 0.0f, 0.0f)),
        IRMath::ivec3(1, 0, 0)
    );
}

TEST(PickingFaceNormal, OriginVoxelMinusZFace) {
    EXPECT_EQ(
        voxelHitFaceNormal(IRMath::vec3(0.0f, 0.0f, -0.4f)),
        IRMath::ivec3(0, 0, -1)
    );
}

TEST(PickingFaceNormal, ExactCenterFavorsXAxisPositive) {
    // Pure center hit: all three component magnitudes equal (zero). The
    // tie-break order (x → y → z) lands on the +X face. Corner hits
    // never arise in practice (ray sample lands strictly inside one cube
    // before the next depth step), so any of the three faces would be a
    // valid place-adjacent direction — this test pins the deterministic
    // choice for regression-detection rather than asserting a semantic.
    EXPECT_EQ(
        voxelHitFaceNormal(IRMath::vec3(0.0f, 0.0f, 0.0f)),
        IRMath::ivec3(1, 0, 0)
    );
}

TEST(PickingFaceNormal, ReturnsSingleNonzeroAxis) {
    // Exhaustively spot-check that the returned normal has exactly one
    // non-zero component — the place-adjacent compute `voxelPos + normal`
    // breaks badly if two axes fire simultaneously.
    const IRMath::vec3 deltas[] = {
        IRMath::vec3(0.49f, 0.40f, 0.30f),   // X dominant
        IRMath::vec3(0.30f, 0.49f, 0.40f),   // Y dominant
        IRMath::vec3(0.30f, 0.40f, 0.49f),   // Z dominant
        IRMath::vec3(-0.49f, -0.40f, -0.30f), // -X dominant
    };
    for (const IRMath::vec3 &delta : deltas) {
        const IRMath::ivec3 normal = voxelHitFaceNormal(delta);
        const int nonzero =
            (normal.x != 0 ? 1 : 0) + (normal.y != 0 ? 1 : 0) + (normal.z != 0 ? 1 : 0);
        EXPECT_EQ(nonzero, 1) << "delta=(" << delta.x << "," << delta.y << "," << delta.z
                              << ") normal=(" << normal.x << "," << normal.y << "," << normal.z
                              << ")";
    }
}

} // namespace

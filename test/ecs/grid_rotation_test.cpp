#include <gtest/gtest.h>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/voxel/grid_rotation.hpp>

#include <set>
#include <tuple>

namespace {

using IRComponents::C_WorldTransform;
using IRPrefab::GridRotation::isIdentityTransform;
using IRPrefab::GridRotation::worldCellForGridVoxel;

constexpr float kEps = 1e-4f;

IRMath::vec4 quatRotateZ(float radians) {
    const float half = radians * 0.5f;
    return IRMath::vec4(0.0f, 0.0f, IRMath::sin(half), IRMath::cos(half));
}

TEST(GridRotationTest, IdentityTransformReturnsTranslateOnly) {
    C_WorldTransform wt;
    wt.translation_ = IRMath::vec3(10.0f, 20.0f, 30.0f);
    EXPECT_TRUE(isIdentityTransform(wt));

    const auto cell = worldCellForGridVoxel(
        IRMath::vec3(1.0f, 2.0f, 3.0f), IRMath::vec3(0.0f), wt
    );
    EXPECT_NEAR(cell.x, 11.0f, kEps);
    EXPECT_NEAR(cell.y, 22.0f, kEps);
    EXPECT_NEAR(cell.z, 33.0f, kEps);
}

TEST(GridRotationTest, IdentityPathDoesNotRound) {
    // Identity defers to UPDATE_VOXEL_SET_CHILDREN's translate-only
    // semantics. Fractional inputs must round-trip unchanged so the helper
    // can be called in place of that system's write without quantizing
    // sub-cell offsets that the legacy path leaves alone.
    C_WorldTransform wt;
    wt.translation_ = IRMath::vec3(0.0f);

    const auto cell = worldCellForGridVoxel(
        IRMath::vec3(0.25f, 0.5f, 0.75f), IRMath::vec3(0.0f), wt
    );
    EXPECT_NEAR(cell.x, 0.25f, kEps);
    EXPECT_NEAR(cell.y, 0.5f, kEps);
    EXPECT_NEAR(cell.z, 0.75f, kEps);
}

TEST(GridRotationTest, OffsetIsAppliedInIdentityPath) {
    // VOXEL_SQUASH_STRETCH writes per-voxel deformation offsets that the
    // translate-only path adds before the parent position. Mirror it.
    C_WorldTransform wt;
    wt.translation_ = IRMath::vec3(5.0f, 0.0f, 0.0f);

    const auto cell = worldCellForGridVoxel(
        IRMath::vec3(1.0f, 0.0f, 0.0f), IRMath::vec3(0.0f, 1.0f, 2.0f), wt
    );
    EXPECT_NEAR(cell.x, 6.0f, kEps);
    EXPECT_NEAR(cell.y, 1.0f, kEps);
    EXPECT_NEAR(cell.z, 2.0f, kEps);
}

TEST(GridRotationTest, NinetyDegreeZRotationMapsXToY) {
    C_WorldTransform wt;
    wt.rotation_ = quatRotateZ(IRMath::kPi * 0.5f);

    const auto cell = worldCellForGridVoxel(
        IRMath::vec3(1.0f, 0.0f, 0.0f), IRMath::vec3(0.0f), wt
    );
    EXPECT_NEAR(cell.x, 0.0f, kEps);
    EXPECT_NEAR(cell.y, 1.0f, kEps);
    EXPECT_NEAR(cell.z, 0.0f, kEps);
}

TEST(GridRotationTest, FortyFiveDegreeZRotationSnapsToGrid) {
    // 45° around Z sends (1,0,0) to (cos45, sin45, 0) ≈ (0.707, 0.707, 0).
    // After grid snapping, both axes round to 1. This is the canonical
    // aliasing case T-294 documents — multiple authored voxels can collapse
    // into the same world cell after rotation; rendering accepts the
    // collision.
    C_WorldTransform wt;
    wt.rotation_ = quatRotateZ(IRMath::kPi * 0.25f);

    const auto cell = worldCellForGridVoxel(
        IRMath::vec3(1.0f, 0.0f, 0.0f), IRMath::vec3(0.0f), wt
    );
    EXPECT_NEAR(cell.x, 1.0f, kEps);
    EXPECT_NEAR(cell.y, 1.0f, kEps);
    EXPECT_NEAR(cell.z, 0.0f, kEps);
}

TEST(GridRotationTest, RotationOccupiesDifferentCellsThanUnrotated) {
    // C6 acceptance criterion #1: a rotated voxel set occupies a different
    // set of world cells than the unrotated one. Use a 4-voxel rod along
    // the +X axis (asymmetric — its bounding box at 30° rotation extends
    // outside the unrotated footprint) so the rotation can't permute
    // back into the same cell set the way a centered symmetric cube does.
    C_WorldTransform unrotated;
    C_WorldTransform rotated;
    rotated.rotation_ = quatRotateZ(IRMath::kPi / 6.0f);

    std::set<std::tuple<int, int, int>> cells0;
    std::set<std::tuple<int, int, int>> cellsRot;
    for (int x = 0; x < 4; ++x) {
        const IRMath::vec3 local{static_cast<float>(x), 0.0f, 0.0f};
        const auto cellA = worldCellForGridVoxel(local, IRMath::vec3(0.0f), unrotated);
        const auto cellB = worldCellForGridVoxel(local, IRMath::vec3(0.0f), rotated);
        cells0.emplace(
            static_cast<int>(cellA.x), static_cast<int>(cellA.y), static_cast<int>(cellA.z)
        );
        cellsRot.emplace(
            static_cast<int>(cellB.x), static_cast<int>(cellB.y), static_cast<int>(cellB.z)
        );
    }
    EXPECT_NE(cells0, cellsRot)
        << "30° rotation must produce a different set of world cells than the unrotated rod";
}

TEST(GridRotationTest, SymmetricCubeRotationIsACellPermutation) {
    // Documents the "aliasing accepted by design" edge case from the C6
    // design note: a 45° (or any cube-symmetric) Z-rotation of a centered
    // integer cube permutes each authored voxel onto another integer cell
    // in the same cube. The visible footprint is unchanged but every
    // per-voxel position moved. Callers needing to assert "the rendered
    // footprint shifted" must use an asymmetric source set (see the rod
    // case above).
    C_WorldTransform unrotated;
    C_WorldTransform rotated;
    rotated.rotation_ = quatRotateZ(IRMath::kPi * 0.25f);

    std::set<std::tuple<int, int, int>> cells0;
    std::set<std::tuple<int, int, int>> cells45;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            const IRMath::vec3 local{static_cast<float>(x), static_cast<float>(y), 0.0f};
            const auto a = worldCellForGridVoxel(local, IRMath::vec3(0.0f), unrotated);
            const auto b = worldCellForGridVoxel(local, IRMath::vec3(0.0f), rotated);
            cells0.emplace(int(a.x), int(a.y), int(a.z));
            cells45.emplace(int(b.x), int(b.y), int(b.z));
        }
    }
    EXPECT_EQ(cells0, cells45) << "45° on a symmetric centered integer cube is a cell permutation";
}

TEST(GridRotationTest, RotationIsDeterministicAcrossInvocations) {
    // C6 acceptance criterion #3: deterministic across frames. Each call
    // with the same inputs must produce bit-identical outputs — the helper
    // is pure, but float ops can drift if reordered, so pin it.
    C_WorldTransform wt;
    wt.rotation_ = quatRotateZ(0.733f); // arbitrary non-special angle
    wt.translation_ = IRMath::vec3(13.0f, 7.0f, -4.0f);
    const IRMath::vec3 local{2.0f, -1.0f, 5.0f};
    const IRMath::vec3 offset{0.0f, 0.5f, 0.0f};

    const auto first = worldCellForGridVoxel(local, offset, wt);
    const auto second = worldCellForGridVoxel(local, offset, wt);
    const auto third = worldCellForGridVoxel(local, offset, wt);
    EXPECT_FLOAT_EQ(first.x, second.x);
    EXPECT_FLOAT_EQ(first.y, second.y);
    EXPECT_FLOAT_EQ(first.z, second.z);
    EXPECT_FLOAT_EQ(first.x, third.x);
}

TEST(GridRotationTest, ScaleAppliesBeforeRotation) {
    // World scale = 2 spreads voxels out 2× before the rotation.
    C_WorldTransform wt;
    wt.scale_ = IRMath::vec3(2.0f);
    wt.rotation_ = quatRotateZ(IRMath::kPi * 0.5f); // 90° around Z

    const auto cell = worldCellForGridVoxel(
        IRMath::vec3(1.0f, 0.0f, 0.0f), IRMath::vec3(0.0f), wt
    );
    // scale: (2,0,0); rotate 90° Z: (0,2,0); round: (0,2,0).
    EXPECT_NEAR(cell.x, 0.0f, kEps);
    EXPECT_NEAR(cell.y, 2.0f, kEps);
    EXPECT_NEAR(cell.z, 0.0f, kEps);
}

TEST(GridRotationTest, TranslationAppliesAfterRotation) {
    // The world translation lands AFTER the rotation so that translating
    // the entity does not move the rotation pivot in world space.
    C_WorldTransform wt;
    wt.rotation_ = quatRotateZ(IRMath::kPi * 0.5f);
    wt.translation_ = IRMath::vec3(100.0f, 50.0f, 25.0f);

    const auto cell = worldCellForGridVoxel(
        IRMath::vec3(1.0f, 0.0f, 0.0f), IRMath::vec3(0.0f), wt
    );
    EXPECT_NEAR(cell.x, 100.0f, kEps);
    EXPECT_NEAR(cell.y, 51.0f, kEps);
    EXPECT_NEAR(cell.z, 25.0f, kEps);
}

} // namespace

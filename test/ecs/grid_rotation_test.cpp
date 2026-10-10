#include <gtest/gtest.h>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/voxel/grid_rotation.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace {

using IRComponents::C_WorldTransform;
using IRPrefab::GridRotation::isIdentityTransform;
using IRPrefab::GridRotation::sourceCellForWorldCell;
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
    // aliasing case documents — multiple authored voxels can collapse
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

TEST(GridRotationTest, SourceCellForWorldCell_RoundTrip_NinetyDegreeZRotation) {
    // Cardinal-rotation round-trip: sourceCellForWorldCell correctly inverts
    // worldCellForGridVoxel when the rotation maps integer cells exactly onto
    // other integer cells (no rounding boundary ambiguity). Pins both the
    // inverse arithmetic and the roundHalfUp convention used to recover the
    // authored cell.
    C_WorldTransform wt;
    wt.rotation_ = quatRotateZ(IRMath::kPi * 0.5f);
    const auto inv = IRMath::quatInverse(wt.rotation_);

    const std::vector<IRMath::ivec3> authored = {
        {1, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, -1, 0}, {2, 3, 4}, {-2, -3, 5},
    };
    for (const auto &a : authored) {
        const auto worldVec = worldCellForGridVoxel(IRMath::vec3(a), IRMath::vec3(0.0f), wt);
        const IRMath::ivec3 worldCell(worldVec);
        const auto source = sourceCellForWorldCell(worldCell, wt, inv);
        EXPECT_EQ(IRMath::roundVec3HalfUp(source), a)
            << "Round-trip failed for authored " << a.x << "," << a.y << "," << a.z;
    }
}

TEST(GridRotationTest, SourceCellForWorldCell_RoundTrip_ScaleAndTranslation) {
    // Full SQT round-trip: 2× uniform scale + 90° Z rotation + translation.
    // 90° keeps the forward map exact (integer → integer); the scale and
    // translation test that sourceCellForWorldCell correctly un-applies all
    // three components of the SQT.
    C_WorldTransform wt;
    wt.scale_ = IRMath::vec3(2.0f);
    wt.rotation_ = quatRotateZ(IRMath::kPi * 0.5f);
    wt.translation_ = IRMath::vec3(7.0f, -3.0f, 5.0f);
    const auto inv = IRMath::quatInverse(wt.rotation_);

    const std::vector<IRMath::ivec3> authored = {
        {1, 0, 0}, {0, 1, 0}, {2, 2, 0}, {1, -1, 3},
    };
    for (const auto &a : authored) {
        const auto worldVec = worldCellForGridVoxel(IRMath::vec3(a), IRMath::vec3(0.0f), wt);
        const IRMath::ivec3 worldCell(worldVec);
        const auto source = sourceCellForWorldCell(worldCell, wt, inv);
        EXPECT_EQ(IRMath::roundVec3HalfUp(source), a)
            << "Round-trip failed for authored " << a.x << "," << a.y << "," << a.z;
    }
}

TEST(GridRotationTest, SourceCellForWorldCell_RoundHalfUpConventionPin) {
    // Pins the roundHalfUp(-0.5) = 0 convention shared with the GPU kernel's
    // `roundHalfUp` helper in c_revoxelize_detached.glsl/.metal.
    // With 90° Z rotation and x-translation of -0.5 the inverse-mapped source
    // has exactly y = -0.5; roundHalfUp correctly returns 0, while
    // glm::round / std::round (half-away-from-zero) would return -1, breaking
    // CPU↔GPU classification agreement.
    C_WorldTransform wt;
    wt.rotation_ = quatRotateZ(IRMath::kPi * 0.5f);
    wt.translation_ = IRMath::vec3(-0.5f, 0.0f, 0.0f);
    const auto inv = IRMath::quatInverse(wt.rotation_);

    const IRMath::ivec3 authored{0, 0, 0};
    // Forward: world = (-0.5, 0, 0); roundHalfUp → (0, 0, 0)
    const IRMath::ivec3 worldCell(
        worldCellForGridVoxel(IRMath::vec3(authored), IRMath::vec3(0.0f), wt)
    );
    EXPECT_EQ(worldCell, IRMath::ivec3(0, 0, 0));

    // Inverse source y = -0.5; roundHalfUp(-0.5) = 0, not -1.
    const auto source = sourceCellForWorldCell(worldCell, wt, inv);
    EXPECT_NEAR(source.y, -0.5f, kEps);
    EXPECT_EQ(IRMath::roundVec3HalfUp(source), authored);
}

using GridCell = std::tuple<int, int, int>;
using GridOccupancy = std::set<GridCell>;
using GridRecords = std::map<GridCell, std::uint8_t>;

enum class GridRotationAxis { Z, Y };

// Membership in the rotated half-open source box uses analytical half-spaces,
// independently of the production quaternion inverse, rounding and AABB walk.
// CENTER's authored -5.5..5.5 positions quantize to source centers -5..6.
GridOccupancy
diagonalBoxOccupancy(int minAxial, int maxAxial, GridRotationAxis axis = GridRotationAxis::Z) {
    constexpr double diagonal = 0.70710678118654752440;
    GridOccupancy cells;
    for (int z = -12; z <= 12; ++z) {
        for (int y = -12; y <= 12; ++y) {
            for (int x = -12; x <= 12; ++x) {
                const double localX = (axis == GridRotationAxis::Z ? x + y : x - z) * diagonal;
                const double localOther = (axis == GridRotationAxis::Z ? y - x : x + z) * diagonal;
                const int axial = axis == GridRotationAxis::Z ? z : y;
                if (localX >= -5.5 && localX < 6.5 && localOther >= -5.5 && localOther < 6.5 &&
                    axial >= minAxial && axial <= maxAxial) {
                    cells.emplace(x, y, z);
                }
            }
        }
    }
    return cells;
}

std::uint8_t occupiedNeighborMask(const GridOccupancy &cells, const GridCell &cell) {
    using namespace IRComponents::VoxelFlags;
    const std::array<GridCell, 6>
        directions{GridCell{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    const std::array<std::uint8_t, 6> bits{
        kFaceOccludedNegX,
        kFaceOccludedPosX,
        kFaceOccludedNegY,
        kFaceOccludedPosY,
        kFaceOccludedNegZ,
        kFaceOccludedPosZ
    };
    const auto [x, y, z] = cell;
    std::uint8_t mask = 0;
    for (std::size_t face = 0; face < directions.size(); ++face) {
        const auto [dx, dy, dz] = directions[face];
        if (cells.contains({x + dx, y + dy, z + dz})) {
            mask |= bits[face];
        }
    }
    return mask;
}

GridOccupancy missingSurfaceCells(const GridOccupancy &expected, const GridRecords &actual) {
    GridOccupancy missing;
    for (const auto &cell : expected) {
        if (occupiedNeighborMask(expected, cell) != IRComponents::VoxelFlags::kFaceOccludedMask &&
            !actual.contains(cell)) {
            missing.insert(cell);
        }
    }
    return missing;
}

void expectGridMasksMatchOccupancy(const GridOccupancy &expected, const GridRecords &actual) {
    for (const auto &[cell, mask] : actual) {
        const auto [x, y, z] = cell;
        SCOPED_TRACE(::testing::Message() << "cell=" << x << "," << y << "," << z);
        EXPECT_TRUE(expected.contains(cell));
        EXPECT_EQ(mask, occupiedNeighborMask(expected, cell));
    }
}

class GridInverseSurfaceTest : public ::testing::Test {
  protected:
    IREntity::EntityManager m_entityManager;

    GridRecords rebuildBox(
        IRMath::ivec3 size, GridRotationAxis axis = GridRotationAxis::Z, bool carvePlate = false
    ) {
        using namespace IRComponents;
        const int capacity = size.x * size.y * size.z;
        const auto canvas = IREntity::createEntity(C_VoxelPool{IRMath::ivec3(capacity + 7, 1, 1)});
        IREntity::getComponent<C_VoxelPool>(canvas).allocateVoxels(7);
        const auto entity = IREntity::createEntity(
            C_VoxelSetNew{size, IRMath::Color{90, 160, 210, 255}, EntityAnchor::CENTER, canvas}
        );
        auto &voxelSet = IREntity::getComponent<C_VoxelSetNew>(entity);
        auto &pool = IREntity::getComponent<C_VoxelPool>(canvas);
        EXPECT_EQ(voxelSet.numVoxels_, capacity);
        EXPECT_EQ(voxelSet.voxelStartIdx_, 7u);
        if (carvePlate) {
            // The -0.5 authored plane rounds to source z=0, matching a centered
            // one-cell plate while preserving the full box's allocation.
            for (int i = 0; i < voxelSet.numVoxels_; ++i) {
                if (voxelSet.positions_[i].pos_.z != -0.5f) {
                    voxelSet.voxels_[i].deactivate();
                }
            }
            pool.resyncActiveMaskFromColors(voxelSet.voxelStartIdx_, capacity);
        }
        C_WorldTransform transform;
        transform.rotation_ = axis == GridRotationAxis::Z
                                  ? quatRotateZ(IRMath::kQuarterPi)
                                  : IRMath::vec4(
                                        0.0f,
                                        IRMath::sin(IRMath::kQuarterPi * 0.5f),
                                        0.0f,
                                        IRMath::cos(IRMath::kQuarterPi * 0.5f)
                                    );
        IRSystem::System<IRSystem::REBUILD_GRID_VOXELS> rebuild;
        GridRecords first;
        for (int pass = 0; pass < 2; ++pass) {
            EXPECT_TRUE(
                rebuild.inverseArm(voxelSet, transform, pool, voxelSet.voxelStartIdx_, capacity)
            );
            GridRecords actual;
            for (int i = 0; i < capacity; ++i) {
                const auto slot = voxelSet.voxelStartIdx_ + static_cast<std::size_t>(i);
                if (pool.getColors()[slot].color_.alpha_ == 0) {
                    continue;
                }
                const auto position = pool.getPositionGlobals()[slot].pos_;
                const GridCell cell{int(position.x), int(position.y), int(position.z)};
                EXPECT_FLOAT_EQ(position.x, static_cast<float>(std::get<0>(cell)));
                EXPECT_FLOAT_EQ(position.y, static_cast<float>(std::get<1>(cell)));
                EXPECT_FLOAT_EQ(position.z, static_cast<float>(std::get<2>(cell)));
                EXPECT_TRUE(
                    actual
                        .emplace(
                            cell,
                            pool.getColors()[slot].flags_ & VoxelFlags::kFaceOccludedMask
                        )
                        .second
                ) << "duplicate destination cell";
            }
            if (pass == 0) {
                first = std::move(actual);
            } else {
                EXPECT_EQ(actual, first) << "repeated rebuild must use authored source records";
            }
        }
        return first;
    }
};

TEST_F(GridInverseSurfaceTest, SolidCubeOverflowRetainsEverySurfaceCellAndMask) {
    for (const auto axis : {GridRotationAxis::Z, GridRotationAxis::Y}) {
        SCOPED_TRACE(axis == GridRotationAxis::Z ? "Z45" : "Y45");
        const auto expected = diagonalBoxOccupancy(-5, 6, axis);
        ASSERT_EQ(expected.size(), 1740u);
        ASSERT_EQ(missingSurfaceCells(expected, {}).size(), 610u);
        const auto actual = rebuildBox(IRMath::ivec3(12), axis);
        ASSERT_EQ(actual.size(), 1728u);
        EXPECT_TRUE(missingSurfaceCells(expected, actual).empty());
        expectGridMasksMatchOccupancy(expected, actual);
        std::size_t omitted = 0;
        for (const auto &cell : expected) {
            if (!actual.contains(cell)) {
                ++omitted;
                EXPECT_EQ(
                    occupiedNeighborMask(expected, cell),
                    IRComponents::VoxelFlags::kFaceOccludedMask
                );
            }
        }
        EXPECT_EQ(omitted, 12u);
    }
}

TEST(GridSurfaceOracleTest, RejectsRemovedExposedPlateCell) {
    const auto expected = diagonalBoxOccupancy(0, 0);
    ASSERT_EQ(expected.size(), 145u);
    ASSERT_EQ(missingSurfaceCells(expected, {}).size(), expected.size());
    GridRecords actual;
    for (const auto &cell : expected) {
        actual.emplace(cell, occupiedNeighborMask(expected, cell));
    }
    ASSERT_TRUE(missingSurfaceCells(expected, actual).empty());
    const GridCell removed{0, 0, 0};
    ASSERT_NE(occupiedNeighborMask(expected, removed), IRComponents::VoxelFlags::kFaceOccludedMask);
    ASSERT_EQ(actual.erase(removed), 1u);
    EXPECT_EQ(missingSurfaceCells(expected, actual), GridOccupancy{removed});
}

TEST_F(GridInverseSurfaceTest, CarvedPlateWithBoxCapacityRetainsEverySurfaceCellAndMask) {
    const auto expected = diagonalBoxOccupancy(0, 0);
    const auto actual = rebuildBox(IRMath::ivec3(12), GridRotationAxis::Z, true);
    ASSERT_EQ(actual.size(), expected.size());
    EXPECT_TRUE(missingSurfaceCells(expected, actual).empty());
    expectGridMasksMatchOccupancy(expected, actual);
}

} // namespace

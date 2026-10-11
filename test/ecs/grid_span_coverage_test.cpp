#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>

#include <array>
#include <map>
#include <set>

namespace {

using Cell = std::array<int, 3>;
using Cells = std::set<Cell>;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
namespace Face = IRComponents::VoxelFlags;

// Independent 45-degree Y inverse transform, with no producer AABB or scratch data.
Cells referenceCells(bool plate) {
    Cells cells;
    const double diagonal = IRMath::sqrt(0.5);
    for (int z = -10; z <= 10; ++z) {
        for (int y = plate ? 0 : -5; y <= (plate ? 0 : 6); ++y) {
            for (int x = -10; x <= 10; ++x) {
                const int sourceX = static_cast<int>(IRMath::floor(diagonal * (x - z) + 0.5));
                const int sourceZ = static_cast<int>(IRMath::floor(diagonal * (x + z) + 0.5));
                if (sourceX >= -5 && sourceX <= 6 && sourceZ >= -5 && sourceZ <= 6) {
                    cells.insert({x, y, z});
                }
            }
        }
    }
    return cells;
}

std::uint8_t referenceFaceMask(const Cells &cells, Cell cell) {
    constexpr std::array<Cell, 6> neighbors{
        {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}}
    };
    constexpr std::array<std::uint8_t, 6> bits{
        Face::kFaceOccludedNegX,
        Face::kFaceOccludedPosX,
        Face::kFaceOccludedNegY,
        Face::kFaceOccludedPosY,
        Face::kFaceOccludedNegZ,
        Face::kFaceOccludedPosZ
    };
    std::uint8_t mask = 0;
    for (std::size_t face = 0; face < neighbors.size(); ++face) {
        const auto &offset = neighbors[face];
        if (cells.contains({cell[0] + offset[0], cell[1] + offset[1], cell[2] + offset[2]})) {
            mask |= bits[face];
        }
    }
    return mask;
}

class GridSpanCoverageTest : public testing::Test {
  protected:
    IREntity::EntityManager m_entities;
    IRSystem::System<IRSystem::REBUILD_GRID_VOXELS> m_rebuild;

    std::map<Cell, std::uint8_t> rebuild(IRMath::ivec3 size, bool carvePlate = false) {
        const auto canvas = IREntity::createEntity(C_VoxelPool{IRMath::ivec3(16)});
        const auto object = IREntity::createEntity(
            C_VoxelSetNew{
                size,
                IRMath::Color{255, 0, 0, 255},
                IRComponents::EntityAnchor::CENTER,
                canvas
            }
        );
        auto &set = IREntity::getComponent<C_VoxelSetNew>(object);
        if (carvePlate) {
            set.carve([](IRMath::vec3 local) { return local.y != -0.5f; });
        }
        IRComponents::C_WorldTransform transform;
        const float halfAngle = IRMath::kPi / 8.0f;
        transform.rotation_ = {0.0f, IRMath::sin(halfAngle), 0.0f, IRMath::cos(halfAngle)};
        // Headless tick: culling remains disabled until a render viewport exists.
        m_rebuild.tick(set, transform, IRComponents::C_RotationMode{});

        const auto &pool = IREntity::getComponent<C_VoxelPool>(canvas);
        std::map<Cell, std::uint8_t> emitted;
        for (int index = 0; index < set.numVoxels_; ++index) {
            const auto slot = set.voxelStartIdx_ + index;
            const auto &voxel = pool.getColors()[slot];
            if (voxel.color_.alpha_ == 0) {
                continue;
            }
            const auto position = pool.getPositionGlobals()[slot].pos_;
            const Cell cell{
                static_cast<int>(position.x),
                static_cast<int>(position.y),
                static_cast<int>(position.z)
            };
            EXPECT_EQ(position, IRMath::vec3(cell[0], cell[1], cell[2]));
            EXPECT_TRUE(emitted.emplace(cell, voxel.flags_ & Face::kFaceOccludedMask).second);
            EXPECT_NE(voxel.reserved_ & IRComponents::VoxelReserved::kRotatedEmit, 0u);
        }
        return emitted;
    }
};

TEST_F(GridSpanCoverageTest, SolidOverflowRetainsEverySurfaceAndFullOccupancyFaceMasks) {
    const auto expected = referenceCells(false);
    const auto emitted = rebuild(IRMath::ivec3(12));
    ASSERT_EQ(expected.size(), 1740u);
    ASSERT_EQ(emitted.size(), 1728u);
    int missingInterior = 0;
    int surface = 0;
    for (const auto &cell : expected) {
        const auto mask = referenceFaceMask(expected, cell);
        const auto found = emitted.find(cell);
        if (mask != Face::kFaceOccludedMask) {
            ++surface;
            EXPECT_NE(found, emitted.end()) << testing::PrintToString(cell);
        }
        if (found == emitted.end()) {
            EXPECT_EQ(mask, Face::kFaceOccludedMask) << testing::PrintToString(cell);
            ++missingInterior;
        }
    }
    EXPECT_EQ(missingInterior, 12);
    EXPECT_EQ(surface, 610);
    for (const auto &[cell, mask] : emitted) {
        EXPECT_TRUE(expected.contains(cell));
        EXPECT_EQ(mask, referenceFaceMask(expected, cell)) << testing::PrintToString(cell);
    }
}

TEST_F(GridSpanCoverageTest, ExactFitPlateDemonstratesSurfaceCapacityLimit) {
    const auto expected = referenceCells(true);
    const auto emitted = rebuild(IRMath::ivec3(12, 1, 12));
    ASSERT_EQ(expected.size(), 145u);
    ASSERT_EQ(emitted.size(), 144u);
    int missingSurface = 0;
    for (const auto &cell : expected) {
        EXPECT_NE(referenceFaceMask(expected, cell), Face::kFaceOccludedMask);
        missingSurface += !emitted.contains(cell);
    }
    EXPECT_EQ(missingSurface, 1);
}

TEST_F(GridSpanCoverageTest, CarvedPlateWithFullBoxSpanRetainsEveryCellAndFace) {
    const auto expected = referenceCells(true);
    const auto emitted = rebuild(IRMath::ivec3(12), true);
    ASSERT_EQ(emitted.size(), expected.size());
    for (const auto &[cell, mask] : emitted) {
        EXPECT_TRUE(expected.contains(cell));
        EXPECT_EQ(mask, referenceFaceMask(expected, cell)) << testing::PrintToString(cell);
    }
}

} // namespace

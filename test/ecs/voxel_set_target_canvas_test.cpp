#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/voxel_pool_api.hpp>

// Covers the entity-keyed voxel-pool path that lets a detached entity
// canvas own and fill its own pool (PR: detached-canvas voxel rendering).
// `C_VoxelSetNew`'s `targetCanvas` argument routes every pool op through a
// specific canvas entity rather than the hardcoded "main" pool, so two
// detached canvases never share voxel storage. Headless: no RenderManager —
// the explicit `targetCanvas` arg bypasses the active-canvas lookup.

namespace {

using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRMath::Color;
using IRMath::ivec3;

class VoxelSetTargetCanvasTest : public ::testing::Test {
  protected:
    IREntity::EntityManager m_entityManager;
};

TEST_F(VoxelSetTargetCanvasTest, VoxelSetAllocatesFromExplicitCanvasPool) {
    const IREntity::EntityId canvas = IREntity::createEntity(C_VoxelPool{ivec3(8, 8, 8)});

    const IREntity::EntityId object = IREntity::createEntity(
        C_VoxelSetNew{ivec3(4, 4, 4), Color{200, 100, 50, 255}, true, canvas}
    );

    const auto &voxelSet = IREntity::getComponent<C_VoxelSetNew>(object);
    EXPECT_EQ(voxelSet.numVoxels_, 64);
    EXPECT_EQ(voxelSet.canvasEntity_, canvas);

    // The 64 voxels landed in the target canvas's pool, not "main".
    const auto &pool = IREntity::getComponent<C_VoxelPool>(canvas);
    EXPECT_EQ(pool.getLiveVoxelCount(), 64);
}

TEST_F(VoxelSetTargetCanvasTest, SeparateCanvasesKeepIsolatedPools) {
    const IREntity::EntityId canvasA = IREntity::createEntity(C_VoxelPool{ivec3(8, 8, 8)});
    const IREntity::EntityId canvasB = IREntity::createEntity(C_VoxelPool{ivec3(8, 8, 8)});

    IREntity::createEntity(C_VoxelSetNew{ivec3(3, 3, 3), Color{255, 0, 0, 255}, true, canvasA});
    IREntity::createEntity(C_VoxelSetNew{ivec3(2, 2, 2), Color{0, 0, 255, 255}, true, canvasB});

    // Each canvas's pool holds only its own voxel set — no cross-pool bleed.
    EXPECT_EQ(IREntity::getComponent<C_VoxelPool>(canvasA).getLiveVoxelCount(), 27);
    EXPECT_EQ(IREntity::getComponent<C_VoxelPool>(canvasB).getLiveVoxelCount(), 8);
}

TEST_F(VoxelSetTargetCanvasTest, EntityKeyedAllocateOnNullCanvasIsEmpty) {
    // A null / pool-less canvas degrades to an empty allocation rather than
    // dereferencing a missing pool.
    const auto allocation = IRPrefab::VoxelPool::allocate(16u, IREntity::kNullEntity);
    EXPECT_EQ(allocation.startIndex_, 0u);
    EXPECT_TRUE(allocation.voxels_.empty());
}

} // namespace
